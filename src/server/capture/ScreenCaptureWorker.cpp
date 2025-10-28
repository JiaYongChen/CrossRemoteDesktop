#include "ScreenCaptureWorker.h"
#include "../../common/core/threading/ThreadSafeQueue.h"
#include "../../common/core/config/Constants.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QPainter>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QDebug>
#include <QtCore/QDateTime>
#include <QtCore/QMetaObject>
#include <algorithm>
#include <chrono>
#include <cmath>

// 日志分类
Q_LOGGING_CATEGORY(screenCaptureWorker, "screencapture.worker")

// ScreenCaptureWorker 实现
ScreenCaptureWorker::ScreenCaptureWorker(QueueManager* queueManager, QObject* parent)
    : Worker(parent)
    , m_queueManager(queueManager)
    , m_primaryScreen(nullptr) {
    qCDebug(screenCaptureWorker) << "ScreenCaptureWorker构造函数: 初始化基础配置";

    // 初始化配置
    m_config.frameRate = CoreConstants::Capture::DEFAULT_FRAME_RATE;
    m_config.highDefinition = true;
    m_config.antiAliasing = true;
    m_config.maxQueueSize = 10; // 仅作为配置保留，不再用于实际队列

    // 计算初始帧延迟
    calculateFrameDelay();

    // 重要：不要在构造函数中创建 QTimer，以避免其隶属于错误线程。
    // 定时器将在 initialize() 中（已处于工作线程）创建并连接。
    qCDebug(screenCaptureWorker) << "ScreenCaptureWorker 构造完成（未创建定时器，等待 initialize()）";
}

ScreenCaptureWorker::~ScreenCaptureWorker() {
    qCDebug(screenCaptureWorker) << "ScreenCaptureWorker析构函数";
    // 析构阶段不再主动调用 stop/等待，生命周期由 ThreadManager 控制，
    // 避免与 destroyThread/stopThread 的停止流程产生竞态。
    qCDebug(screenCaptureWorker, "ScreenCaptureWorker 析构完成");
}

bool ScreenCaptureWorker::initialize() {
    qCInfo(screenCaptureWorker, "初始化 ScreenCaptureWorker");

    // 检查并缓存主屏幕
    QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if ( !app ) {
        qCWarning(screenCaptureWorker, "未检测到QGuiApplication实例，某些功能可能受限");
    }
    m_primaryScreen = app ? app->primaryScreen() : nullptr;
    if ( m_primaryScreen ) {
        m_screenGeometry = m_primaryScreen->geometry();
    }
    qCDebug(screenCaptureWorker, "Primary Screen geometry: %d,%d %dx%d",
        m_screenGeometry.x(), m_screenGeometry.y(),
        m_screenGeometry.width(), m_screenGeometry.height());
    if ( m_config.captureRect.isEmpty() ) {
        m_config.captureRect = m_screenGeometry;
    }
    {
        QMutexLocker locker(&m_statsMutex);
        m_stats = CaptureStats();
        m_frameTimestamps.clear();
    }

    // 依据配置计算帧间隔
    calculateFrameDelay();

    // 在工作线程中创建并配置统计定时器
    if ( !m_statsTimer ) {
        m_statsTimer = new QTimer(this);
        m_statsTimer->setInterval(STATS_UPDATE_INTERVAL);
        m_statsTimer->setSingleShot(false);
        m_statsTimer->stop();
        disconnect(m_statsTimer, &QTimer::timeout, this, &ScreenCaptureWorker::updateStats);
    }

    // 初始化捕获定时器（用于测试环境或未启动Worker线程时驱动performCapture）
    if ( !m_captureTimer ) {
        m_captureTimer = new QTimer(this);
        m_captureTimer->setTimerType(Qt::PreciseTimer);
        m_captureTimer->stop();
        QObject::connect(m_captureTimer, &QTimer::timeout,
            this, &ScreenCaptureWorker::performCapture,
            Qt::UniqueConnection);
    }

    qCInfo(screenCaptureWorker, "ScreenCaptureWorker 初始化成功");
    return true;
}

void ScreenCaptureWorker::cleanup() {
    qCInfo(screenCaptureWorker, "清理 ScreenCaptureWorker 资源");
    if ( m_statsTimer ) {
        m_statsTimer->stop();
    }
    // 停止捕获定时器并断开，避免析构后回调
    if ( m_captureTimer ) {
        if ( m_captureTimer->isActive() ) {
            m_captureTimer->stop();
        }
        QObject::disconnect(m_captureTimer, &QTimer::timeout, this, &ScreenCaptureWorker::performCapture);
    }
    m_isCapturing.store(false);
    {
        QMutexLocker locker(&m_statsMutex);
        m_captureTimeHistory.clear();
        m_frameTimestamps.clear();
    }
    qCInfo(screenCaptureWorker, "ScreenCaptureWorker 资源清理完成");
}

void ScreenCaptureWorker::startCapturing() {
    m_isCapturing.store(true);
    auto startFn = [this]() {
        // 若尚未初始化（定时器未创建），自动进行一次初始化，以便在非线程环境下也能正常工作
        if ( !m_statsTimer || !m_captureTimer ) {
            initialize();
        }
        if ( !m_statsTimer ) return;
        QObject::connect(m_statsTimer, &QTimer::timeout, this, &ScreenCaptureWorker::updateStats, Qt::UniqueConnection);
        if ( !m_statsTimer->isActive() ) {
            m_statsTimer->start();
        }
        // 启动捕获定时器（在测试环境或未启动Worker线程时用于驱动捕获）
        if ( m_captureTimer ) {
            calculateFrameDelay();
            m_captureTimer->setTimerType(Qt::PreciseTimer);
            m_captureTimer->setInterval(static_cast<int>(m_frameDelay.count()));
            if ( !m_captureTimer->isActive() ) {
                m_captureTimer->start();
            }
        }
        qCDebug(screenCaptureWorker) << "startCapturing: 捕获已开始，统计定时器/捕获定时器已启动";
    };
    if ( QThread::currentThread() == this->thread() ) {
        startFn();
    } else {
        QMetaObject::invokeMethod(this, startFn, Qt::QueuedConnection);
    }
}

void ScreenCaptureWorker::stopCapturing() {
    // 立即设置停止标志
    m_isCapturing.store(false);

    // 立即停止定时器，不使用异步调用以确保立即生效
    auto stopFn = [this]() {
        if ( m_statsTimer && m_statsTimer->isActive() ) {
            m_statsTimer->stop();
        }
        if ( m_statsTimer ) {
            QObject::disconnect(m_statsTimer, &QTimer::timeout, this, &ScreenCaptureWorker::updateStats);
        }

        // 停止捕获定时器并断开信号，避免多余触发
        if ( m_captureTimer && m_captureTimer->isActive() ) {
            m_captureTimer->stop();
        }
        if ( m_captureTimer ) {
            QObject::disconnect(m_captureTimer, &QTimer::timeout, this, &ScreenCaptureWorker::performCapture);
        }
        qCDebug(screenCaptureWorker) << "stopCapturing: 捕获已停止，统计/捕获定时器已停止并断开信号";
    };

    // 如果在Worker线程中，立即执行；否则使用同步调用确保立即完成
    if ( QThread::currentThread() == this->thread() ) {
        stopFn();
    } else {
        // 使用BlockingQueuedConnection确保立即完成
        QMetaObject::invokeMethod(this, stopFn, Qt::BlockingQueuedConnection);
    }
}

void ScreenCaptureWorker::processTask() {
    try {
        // 在任务开始处快速响应停止请求，避免进入不必要的捕获流程
        if ( shouldStop() ) {
            return;
        }
        // 统一由performCapture执行一次捕获；测试环境下也需要生成模拟帧
        if ( m_isCapturing.load() ) {
            // 按帧间隔节流，避免过于频繁
            if ( shouldCaptureFrame() ) {
                // 在调用捕获前再次检查停止，尽可能减少进入重型操作的机会
                if ( shouldStop() ) {
                    return;
                }
                performCapture();
            } else {
                // 未到帧间隔，短暂休眠让出CPU
                QThread::msleep(1);
            }
        } else {
            // 未处于捕获状态时，轻量休眠避免空转
            QThread::msleep(2);
        }

        if ( m_configChanged.load() ) {
            calculateFrameDelay();
            m_configChanged.store(false);
            qCDebug(screenCaptureWorker, "配置已更新，新帧延迟: %lld ms", m_frameDelay.count());
        }
    } catch ( const std::exception& e ) {
        qCCritical(screenCaptureWorker, "Exception in ScreenCaptureWorker::processTask: %s", e.what());
        handleCaptureError(QString("ProcessTask exception: %1").arg(e.what()));
    } catch ( ... ) {
        qCCritical(screenCaptureWorker, "Unknown exception in ScreenCaptureWorker::processTask");
        handleCaptureError("ProcessTask unknown exception");
    }
}

void ScreenCaptureWorker::performCapture() {
    // 在函数入口处立即检查停止请求，尽快退出
    if ( shouldStop() ) {
        return;
    }
    if ( !m_isCapturing.load() ) {
        return;
    }
    // 基于帧间隔判断是否应当捕获，避免过于频繁的timeout导致的过采样
    if ( !shouldCaptureFrame() ) {
        return;
    }
    // 捕获前再次检查停止，避免进入潜在耗时的屏幕抓取
    if ( shouldStop() ) {
        return;
    }
    auto captureStartTime = std::chrono::steady_clock::now();
    try {
        QImage capturedImage = captureScreen();
        // 捕获后立刻检查停止请求，防止后续处理占用时间
        if ( shouldStop() ) {
            return;
        }
        if ( capturedImage.isNull() ) {
            handleCaptureError("捕获的图像为空");
            return;
        }

        // 记录捕获耗时
        auto captureEndTime = std::chrono::steady_clock::now();
        auto captureTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            captureEndTime - captureStartTime);
        recordCaptureTime(captureTime);
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.totalFramesCaptured++;
            m_frameTimestamps.push_back(QDateTime::currentMSecsSinceEpoch());
            if ( m_frameTimestamps.size() > MAX_FRAME_TIMESTAMP_HISTORY ) {
                m_frameTimestamps.pop_front();
            }
        }
        // 获取当前时间戳
        qint64 timestamp = QDateTime::currentMSecsSinceEpoch();

        // 如果有队列管理器，将帧放入捕获队列
        if ( m_queueManager ) {
            CapturedFrame frame;
            frame.image = capturedImage;
            frame.timestamp = QDateTime::fromMSecsSinceEpoch(timestamp);
            frame.frameId = m_stats.totalFramesCaptured;
            frame.originalSize = capturedImage.size();
            
            auto captureQueue = m_queueManager->getCaptureQueue();
            if ( captureQueue ) {
                // 使用丢弃旧帧策略入队，当队列满时自动丢弃最旧的帧
                // 这确保始终处理最新的屏幕捕获数据，适合实时远程桌面场景
                bool enqueued = captureQueue->enqueueDropOldest(frame);
                if ( enqueued ) {
                    //qCDebug(screenCaptureWorker, "成功将帧放入捕获队列，帧ID: %llu", frame.frameId);
                } else {
                    qCWarning(screenCaptureWorker, "捕获队列已停止，无法入队，丢弃帧ID: %llu", frame.frameId);
                    QMutexLocker locker(&m_statsMutex);
                    m_stats.droppedFrames++;
                }
            } else {
                qCWarning(screenCaptureWorker, "捕获队列不可用（未初始化），丢弃帧ID: %llu", frame.frameId);
                QMutexLocker locker(&m_statsMutex);
                m_stats.droppedFrames++;
            }
        }

        // qCDebug(screenCaptureWorker, "成功捕获帧，大小: %dx%d，耗时: %lld ms",
        //     capturedImage.width(), capturedImage.height(), captureTime.count());
    } catch ( const std::exception& e ) {
        handleCaptureError(QString("捕获异常: %1").arg(e.what()));
    } catch ( ... ) {
        handleCaptureError("未知捕获异常");
    }
    m_lastCaptureTime = std::chrono::steady_clock::now();
}

QImage ScreenCaptureWorker::captureScreen() {
    // 在屏幕抓取前检查停止请求，若已请求停止则立即返回空图像
    if ( shouldStop() ) {
        return QImage();
    }

    if ( !m_primaryScreen ) {
        qCWarning(screenCaptureWorker, "主屏幕指针为空");
        return QImage();
    }

    // 使用完整的屏幕区域，不使用配置的捕获区域
    QRect captureRect = m_screenGeometry;
    if ( captureRect.isEmpty() ) {
        qCWarning(screenCaptureWorker, "屏幕区域无效");
        return QImage();
    }

    // 在调用潜在耗时的 grabWindow 前再次检查停止请求
    if ( shouldStop() ) {
        return QImage();
    }

    // 直接在当前线程执行屏幕抓取
    QPixmap pixmap = m_primaryScreen->grabWindow(0,
        captureRect.x(),
        captureRect.y(),
        captureRect.width(),
        captureRect.height());

    if ( pixmap.isNull() ) {
        qCWarning(screenCaptureWorker, "屏幕捕获失败");
        return QImage();
    }

    return pixmap.toImage();
}

QImage ScreenCaptureWorker::captureScreenRegion(const QRect& /*region*/) {
    if ( !m_primaryScreen ) {
        return QImage();
    }

    // 使用完整的屏幕区域，忽略传入的区域参数
    QRect captureRect = m_screenGeometry;
    if ( captureRect.isEmpty() ) {
        return QImage();
    }

    // 直接在当前线程执行屏幕抓取
    QPixmap pixmap = m_primaryScreen->grabWindow(0,
        captureRect.x(),
        captureRect.y(),
        captureRect.width(),
        captureRect.height());

    if ( pixmap.isNull() ) {
        return QImage();
    }

    return pixmap.toImage();
}

void ScreenCaptureWorker::calculateFrameDelay() {
    int fps;
    {
        QMutexLocker locker(&m_configMutex);
        fps = m_config.frameRate;
    }
    fps = std::clamp(fps, MIN_FRAME_RATE, MAX_FRAME_RATE);
    m_frameDelay = std::chrono::milliseconds(1000 / fps);
    qCDebug(screenCaptureWorker, "计算帧延迟: %d fps -> %lld ms", fps, m_frameDelay.count());
}

bool ScreenCaptureWorker::shouldCaptureFrame() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCaptureTime);
    return elapsed >= m_frameDelay;
}

void ScreenCaptureWorker::recordCaptureTime(std::chrono::milliseconds time) {
    QMutexLocker locker(&m_statsMutex);
    if ( time > m_stats.maxCaptureTime ) {
        m_stats.maxCaptureTime = time;
    }
    if ( time < m_stats.minCaptureTime ) {
        m_stats.minCaptureTime = time;
    }
    m_captureTimeHistory.push_back(time);
    if ( m_captureTimeHistory.size() > MAX_CAPTURE_TIME_HISTORY ) {
        m_captureTimeHistory.pop_front();
    }
    if ( !m_captureTimeHistory.empty() ) {
        auto total = std::accumulate(m_captureTimeHistory.begin(),
            m_captureTimeHistory.end(),
            std::chrono::milliseconds(0));
        m_stats.avgCaptureTime = total / m_captureTimeHistory.size();
    }
}

void ScreenCaptureWorker::updateFrameRate() {
    QMutexLocker locker(&m_statsMutex);
    if ( m_frameTimestamps.size() < 2 ) {
        m_stats.currentFrameRate = 0.0;
        return;
    }
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 oneSecondAgo = currentTime - 1000;
    int framesInLastSecond = 0;
    for ( auto it = m_frameTimestamps.rbegin(); it != m_frameTimestamps.rend(); ++it ) {
        if ( *it >= oneSecondAgo ) {
            framesInLastSecond++;
        } else {
            break;
        }
    }
    m_stats.currentFrameRate = static_cast<double>(framesInLastSecond);
}

void ScreenCaptureWorker::monitorResourceUsage() {
    QMutexLocker locker(&m_statsMutex);
    m_stats.cpuUsage = 0.0; // 平台相关实现留空
    m_stats.memoryUsage = 0; // 平台相关实现留空
}

void ScreenCaptureWorker::handleCaptureError(const QString& error) {
    qCWarning(screenCaptureWorker, "捕获错误: %s", qPrintable(error));
    m_lastError = error;
    m_errorCount.fetch_add(1);
    if ( m_errorCount.load() > MAX_ERROR_COUNT ) {
        m_recoveryMode.store(true);
        qCCritical(screenCaptureWorker, "错误次数过多，进入恢复模式");
    }
}

bool ScreenCaptureWorker::recoverFromError() {
    // 简化恢复策略：重置错误计数与恢复标志
    m_errorCount.store(0);
    m_recoveryMode.store(false);
    return true;
}

void ScreenCaptureWorker::updateStats() {
    updateFrameRate();
    monitorResourceUsage();
    CaptureStats snapshot;
    {
        QMutexLocker locker(&m_statsMutex);
        snapshot = m_stats;
    }
    emit captureStatsUpdated(snapshot);
}

void ScreenCaptureWorker::updateConfig(const CaptureConfig& config) {
    CaptureConfig normalized = config;
    {
        // 边界裁剪：帧率
        if ( normalized.frameRate < MIN_FRAME_RATE ) normalized.frameRate = MIN_FRAME_RATE;
        if ( normalized.frameRate > MAX_FRAME_RATE ) normalized.frameRate = MAX_FRAME_RATE;
        QMutexLocker locker(&m_configMutex);
        m_config = normalized;
    }
    m_configChanged.store(true);
    // 若捕获定时器正在运行，则根据新配置动态调整间隔
    if ( m_captureTimer && m_captureTimer->isActive() ) {
        calculateFrameDelay();
        m_captureTimer->setInterval(static_cast<int>(m_frameDelay.count()));
    }
}

CaptureConfig ScreenCaptureWorker::getCurrentConfig() const {
    QMutexLocker locker(&m_configMutex);
    return m_config;
}

CaptureStats ScreenCaptureWorker::getCaptureStats() const {
    QMutexLocker locker(&m_statsMutex);
    return m_stats;
}