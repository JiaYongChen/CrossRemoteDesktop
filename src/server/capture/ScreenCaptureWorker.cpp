#include "ScreenCaptureWorker.h"
#include "../../common/config/CaptureConstants.h"
#include "../../common/config/ProcessingConstants.h"
#include "../../common/logging/LoggingCategories.h"
#include "../dataflow/QueueManager.h"
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QPainter>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QDateTime>
#include <QtCore/QMetaObject>
#include <algorithm>
#include <chrono>
#include <cmath>


// ScreenCaptureWorker 实现
ScreenCaptureWorker::ScreenCaptureWorker(QueueManager* queueManager, QObject* parent)
    : Worker(parent)
    , m_queueManager(queueManager)
    , m_primaryScreen(nullptr) {
    qCDebug(lcServerCapture) << "ScreenCaptureWorker构造函数: 初始化基础配置";

    // 初始化配置
    m_config.frameRate = CaptureConstants::DefaultFrameRate;
    m_config.highDefinition = true;
    m_config.antiAliasing = true;
    m_config.maxQueueSize = 10; // 仅作为配置保留，不再用于实际队列

    // 计算初始帧延迟
    calculateFrameDelay();

    // 重要：不要在构造函数中创建 QTimer，以避免其隶属于错误线程。
    // 定时器将在 initialize() 中（已处于工作线程）创建并连接。
    qCDebug(lcServerCapture) << "ScreenCaptureWorker 构造完成（未创建定时器，等待 initialize()）";
}

ScreenCaptureWorker::~ScreenCaptureWorker() {
    qCDebug(lcServerCapture) << "ScreenCaptureWorker析构函数";
    // 析构阶段不再主动调用 stop/等待，生命周期由 ThreadManager 控制，
    // 避免与 destroyThread/stopThread 的停止流程产生竞态。
    qCDebug(lcServerCapture) << "ScreenCaptureWorker 析构完成";
}

bool ScreenCaptureWorker::initialize() {
    // 防重复初始化：startCapturing 和 doStart 可能在 Worker 启动阶段
    // 通过 QueuedConnection 竞相调用 initialize()，导致 D3D11 设备被
    // 创建两次。只允许第一次初始化生效。
    if (m_initialized.exchange(true)) {
        qCDebug(lcServerCapture) << "ScreenCaptureWorker 已初始化，跳过";
        return true;
    }
    qCInfo(lcServerCapture) << "初始化 ScreenCaptureWorker";

    // 检查并缓存主屏幕
    QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if ( !app ) {
        qCWarning(lcServerCapture) << "未检测到QGuiApplication实例，某些功能可能受限";
    }
    m_primaryScreen = app ? app->primaryScreen() : nullptr;
    if ( m_primaryScreen ) {
        m_screenGeometry = m_primaryScreen->geometry();
    }
    qCDebug(lcServerCapture) << "Primary Screen geometry:" << m_screenGeometry.x()
        << "," << m_screenGeometry.y() << m_screenGeometry.width() << "x" << m_screenGeometry.height();
    if ( m_config.captureRect.isEmpty() ) {
        m_config.captureRect = m_screenGeometry;
    }
    // 依据配置计算帧间隔
    calculateFrameDelay();

    // 初始化捕获定时器（用于测试环境或未启动Worker线程时驱动performCapture）
    if ( !m_captureTimer ) {
        m_captureTimer = new QTimer(this);
        m_captureTimer->setTimerType(Qt::PreciseTimer);
        m_captureTimer->stop();
        QObject::connect(m_captureTimer, &QTimer::timeout,
            this, &ScreenCaptureWorker::performCapture,
            Qt::UniqueConnection);
    }

    // Initialize DXGI capture engine (Windows only)
#ifdef Q_OS_WIN
    m_dxgiCapture = std::make_unique<DxgiCapture>();
    if ( m_dxgiCapture->initialize() ) {
        m_dxgiAvailable = true;
        m_dxgiReinitAttempts = 0;
        qCInfo(lcServerCapture) << "DXGI Desktop Duplication initialized, desktop:"
            << m_dxgiCapture->desktopSize();
    } else {
        m_dxgiAvailable = false;
        qCWarning(lcServerCapture) << "DXGI initialization failed:"
            << m_dxgiCapture->lastError()
            << "— falling back to QScreen::grabWindow()";
    }
#endif

    qCInfo(lcServerCapture) << "ScreenCaptureWorker 初始化成功";
    return true;
}

void ScreenCaptureWorker::cleanup() {
    // 防重复清理：Worker::doStop() 和 Worker::stop() 的 force-stop QTimer
    // 可能竞相调用 cleanup()，导致 m_dxgiCapture->shutdown() 在已释放的
    // COM 对象上崩溃 (0xC0000005)。
    if (m_cleanedUp.exchange(true)) {
        qCDebug(lcServerCapture) << "ScreenCaptureWorker 已清理，跳过";
        return;
    }
    qCInfo(lcServerCapture) << "清理 ScreenCaptureWorker 资源";
#ifdef Q_OS_WIN
    if ( m_dxgiCapture ) {
        m_dxgiCapture->shutdown();
        m_dxgiCapture.reset();
        m_dxgiAvailable = false;
    }
#endif
    // 停止捕获定时器并断开，避免析构后回调
    if ( m_captureTimer ) {
        if ( m_captureTimer->isActive() ) {
            m_captureTimer->stop();
        }
        QObject::disconnect(m_captureTimer, &QTimer::timeout, this, &ScreenCaptureWorker::performCapture);
    }
    m_isCapturing.store(false);
    qCInfo(lcServerCapture) << "ScreenCaptureWorker 资源清理完成";
}

void ScreenCaptureWorker::startCapturing() {
    m_isCapturing.store(true);
    auto startFn = [this]() {
        // 若尚未初始化（定时器未创建），自动进行一次初始化，以便在非线程环境下也能正常工作
        if ( !m_captureTimer ) {
            initialize();
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
        qCInfo(lcServerCapture) << "startCapturing: 捕获已开始，捕获定时器已启动";
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

        // 停止捕获定时器并断开信号，避免多余触发
        if ( m_captureTimer && m_captureTimer->isActive() ) {
            m_captureTimer->stop();
        }
        if ( m_captureTimer ) {
            QObject::disconnect(m_captureTimer, &QTimer::timeout, this, &ScreenCaptureWorker::performCapture);
        }
        qCInfo(lcServerCapture) << "stopCapturing: 捕获已停止，捕获定时器已停止并断开信号";
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
                // DXGI 超时（桌面无变化）会快速返回 null 不产生帧 —
                // 此时 setDidWork(true) 仍适度，因为我们已经完成了一次检查。
                // workLoop 的 1ms 睡眠足够短不会造成明显延迟。
                setDidWork(true);
            } else {
                // 帧间隔期间独立高频采样光标位置，不受帧率节流限制
                sampleCursorPosition();
                QThread::msleep(1);
                setDidWork(false);
            }
        } else {
            // 未处于捕获状态时，轻量休眠避免空转
            QThread::msleep(2);
            setDidWork(false);
        }

        if ( m_configChanged.load() ) {
            calculateFrameDelay();
            m_configChanged.store(false);
            qCDebug(lcServerCapture) << "配置已更新，新帧延迟: " << m_frameDelay.count() << " ms";
        }
    } catch ( const std::exception& e ) {
        qCCritical(lcServerCapture) << "Exception in ScreenCaptureWorker::processTask: " << e.what();
        handleCaptureError(QString("ProcessTask exception: %1").arg(e.what()));
    } catch ( ... ) {
        qCCritical(lcServerCapture) << "Unknown exception in ScreenCaptureWorker::processTask";
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
    try {
        QImage capturedImage;
        CursorMessage cursorMsg;

        // DXGI fast path — captures frame + cursor in one call
#ifdef Q_OS_WIN
        if ( m_dxgiAvailable && m_dxgiCapture ) {
            CaptureResult result = m_dxgiCapture->captureFrame(5);

            // 光标独立于帧：屏幕静止时 captureFrame 也返回光标（回退路径）
            if ( result.cursor.width > 0 ) {
                cursorMsg = std::move(result.cursor);
            }
            if ( !result.frame.isNull() ) {
                m_dxgiReinitAttempts = 0;
                capturedImage = std::move(result.frame);
            } else if ( !m_dxgiCapture->isInitialized() ) {
                // DXGI access-lost — attempt reinit
                qCWarning(lcServerCapture) << "DXGI access lost, attempting reinitialize"
                    << "(attempt" << (m_dxgiReinitAttempts + 1) << "/" << CaptureConstants::MaxDxgiReinitAttempts << ")";
                ++m_dxgiReinitAttempts;
                if ( m_dxgiReinitAttempts <= CaptureConstants::MaxDxgiReinitAttempts ) {
                    if ( m_dxgiCapture->reinitialize() ) {
                        m_dxgiReinitAttempts = 0;
                        qCInfo(lcServerCapture) << "DXGI reinitialized successfully";
                        CaptureResult retryResult = m_dxgiCapture->captureFrame(5);
                        if ( !retryResult.frame.isNull() ) {
                            capturedImage = std::move(retryResult.frame);
                            if ( retryResult.cursor.width > 0 ) {
                                cursorMsg = std::move(retryResult.cursor);
                            }
                        }
                    }
                } else {
                    qCCritical(lcServerCapture) << "DXGI reinit attempts exhausted, falling back";
                    m_dxgiAvailable = false;
                }
            }
            // else: DXGI healthy but timeout — capturedImage stays null, skip GDI fallback
        }
#endif

        // Fallback to GDI if DXGI didn't produce a frame
        if ( capturedImage.isNull() ) {
#ifdef Q_OS_WIN
            // Only fallback when DXGI is genuinely unavailable, not just timeout
            if ( !m_dxgiAvailable || !m_dxgiCapture || !m_dxgiCapture->isInitialized() )
#endif
            {
                capturedImage = captureScreen();
            }
        }

        // 捕获后立刻检查停止请求，防止后续处理占用时间
        if ( shouldStop() ) {
            return;
        }
        // 光标数据独立于帧数据：桌面静止时 DXGI 超时无帧，但光标位置仍需发送
        if ( cursorMsg.width > 0 ) {
            m_lastCursorSampleTime = std::chrono::steady_clock::now();  // 同步采样时钟
            static int s_cursorEmitCount = 0;
            ++s_cursorEmitCount;
            if (s_cursorEmitCount <= 20 || s_cursorEmitCount % 60 == 0)
                qCDebug(lcServerCapture) << "[CURSOR-TRACE] SERVER emit #" << s_cursorEmitCount
                    << "pos:" << cursorMsg.posX << "," << cursorMsg.posY
                    << "size:" << cursorMsg.width << "x" << cursorMsg.height;
            emit cursorUpdateReady(std::move(cursorMsg));
        }
        if ( capturedImage.isNull() ) {
            // DXGI 超时（桌面无变化）是正常情况，不是错误。
            // 仅当 DXGI 不可用或已失效时才记录错误。
#ifdef Q_OS_WIN
            if ( !m_dxgiAvailable || !m_dxgiCapture || !m_dxgiCapture->isInitialized() )
#endif
            {
                handleCaptureError("捕获的图像为空");
            }
            return;
        }

        ++m_totalFramesCaptured;

        // 获取当前时间戳
        qint64 timestamp = QDateTime::currentMSecsSinceEpoch();

        // 如果有队列管理器，将帧放入捕获队列
        if ( m_queueManager ) {
            CapturedFrame frame;
            frame.originalSize = capturedImage.size();
            frame.image = std::make_shared<QImage>(std::move(capturedImage));
            frame.timestamp = QDateTime::fromMSecsSinceEpoch(timestamp);
            frame.frameId = m_totalFramesCaptured;

            if ( m_queueManager->enqueueCapturedFrame(frame) ) {
                emit frameEnqueued();
            } else {
                qCDebug(lcServerCapture) << "捕获队列已停止，无法入队，丢弃帧ID: " << frame.frameId;
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

void ScreenCaptureWorker::sampleCursorPosition() {
#ifdef Q_OS_WIN
    if (!m_dxgiAvailable || !m_dxgiCapture) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastCursorSampleTime);
    if (elapsed.count() < CaptureConstants::CursorSampleIntervalMs) {
        return;  // 未到采样间隔
    }
    m_lastCursorSampleTime = now;

    CursorMessage cursor = m_dxgiCapture->sampleCursorPosition();
    if (cursor.width > 0) {
        static int s_fastSampleCount = 0;
        ++s_fastSampleCount;
        if (s_fastSampleCount <= 20 || s_fastSampleCount % 120 == 0)
            qCDebug(lcServerCapture) << "[CURSOR-TRACE] SERVER fast-sample #"
                << s_fastSampleCount << "pos:" << cursor.posX << "," << cursor.posY;
        emit cursorUpdateReady(std::move(cursor));
    }
#endif
}

QImage ScreenCaptureWorker::captureScreen() {
    // Qt fallback path — QScreen::grabWindow() (GDI-based)
    if ( !m_primaryScreen ) {
        qCWarning(lcServerCapture) << "主屏幕指针为空";
        return QImage();
    }

    QRect captureRect = m_screenGeometry;
    if ( captureRect.isEmpty() ) {
        qCWarning(lcServerCapture) << "屏幕区域无效";
        return QImage();
    }

    if ( shouldStop() ) {
        return QImage();
    }

    QPixmap pixmap = m_primaryScreen->grabWindow(0,
        captureRect.x(),
        captureRect.y(),
        captureRect.width(),
        captureRect.height());

    if ( pixmap.isNull() ) {
        qCWarning(lcServerCapture) << "屏幕捕获失败";
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
    fps = std::clamp(fps, CaptureConstants::MinFrameRate, CaptureConstants::MaxFrameRate);
    m_frameDelay = std::chrono::milliseconds(1000 / fps);
    qCDebug(lcServerCapture) << "计算帧延迟: " << fps << " fps -> " << m_frameDelay.count() << " ms";
}

bool ScreenCaptureWorker::shouldCaptureFrame() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCaptureTime);
    return elapsed >= m_frameDelay;
}




void ScreenCaptureWorker::handleCaptureError(const QString& error) {
    qCWarning(lcServerCapture) << "捕获错误: " << error;
    m_lastError = error;
    m_errorCount.fetch_add(1);
    if ( m_errorCount.load() > CaptureConstants::MaxErrorCount ) {
        m_recoveryMode.store(true);
        qCCritical(lcServerCapture) << "错误次数过多，进入恢复模式";
    }
}

bool ScreenCaptureWorker::recoverFromError() {
    // 简化恢复策略：重置错误计数与恢复标志
    m_errorCount.store(0);
    m_recoveryMode.store(false);
    return true;
}


void ScreenCaptureWorker::updateConfig(const CaptureConfig& config) {
    CaptureConfig normalized = config;
    {
        // 边界裁剪：帧率
        if ( normalized.frameRate < CaptureConstants::MinFrameRate ) normalized.frameRate = CaptureConstants::MinFrameRate;
        if ( normalized.frameRate > CaptureConstants::MaxFrameRate ) normalized.frameRate = CaptureConstants::MaxFrameRate;
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

