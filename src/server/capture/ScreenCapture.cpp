#include "server/capture/ScreenCapture.h"

#include <algorithm>
#include <memory>

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

#include "common/config/CaptureConstants.h"
#include "common/error/ErrorCode.h"
#include "common/error/RdError.h"
#include "common/logging/LoggingCategories.h"
#include "common/threading/ThreadManager.h"
#include "server/capture/ScreenCaptureWorker.h"
#include "server/dataflow/QueueManager.h"


ScreenCapture::ScreenCapture(ThreadManager* threadMgr, QueueManager* queueMgr, QObject* parent)
    : QObject(parent)
    , m_threadManager(threadMgr)
    , m_queueManager(queueMgr)
    , m_isCapturing(false) {
    qCDebug(lcServerCapture) << "ScreenCapture 多线程管理器构造函数调用";

    // 初始化默认配置
    m_captureConfig.frameRate = CaptureConstants::DefaultFrameRate;
    m_captureConfig.highDefinition = true;
    m_captureConfig.antiAliasing = true;
    m_captureConfig.highScaleQuality = true;
    m_captureConfig.captureRect = QRect(); // 空矩形表示全屏

    // 确保队列管理器已初始化
    if ( !m_queueManager ) {
        qCCritical(lcServerCapture) << "QueueManager 为空，队列功能不可用";
        return;
    }

    // 连接ThreadManager信号以监控线程状态
    connect(m_threadManager, &ThreadManager::threadStarted, this, &ScreenCapture::onThreadStarted);
    connect(m_threadManager, &ThreadManager::threadStopped, this, &ScreenCapture::onThreadStopped);
    connect(m_threadManager, &ThreadManager::threadError, this, &ScreenCapture::onThreadError);
    connect(m_threadManager, &ThreadManager::threadRestarted, this, &ScreenCapture::onThreadRestarted);

    qCDebug(lcServerCapture) << "ScreenCapture 多线程管理器构造完成";
}

ScreenCapture::~ScreenCapture() {
    qCDebug(lcServerCapture) << "ScreenCapture 多线程管理器析构函数调用";

    // 停止捕获
    stopCapture();

    // 清理线程资源
    cleanupThreads();

    qCDebug(lcServerCapture) << "ScreenCapture 多线程管理器析构完成";
}

void ScreenCapture::startCapture() {
    if ( m_isCapturing.load() ) {
        qCDebug(lcServerCapture) << "已在捕获中，忽略启动请求";
        return;
    }

    int currentFrameRate;
    {
        QMutexLocker locker(&m_configMutex);
        currentFrameRate = m_captureConfig.frameRate;
    }
    qCInfo(lcServerCapture) << "启动多线程屏幕捕获，帧率:" << currentFrameRate;

    // 初始化线程架构
    if ( !initializeThreads() ) {
        qCCritical(lcServerCapture) << "线程初始化失败，无法启动捕获";
        return;
    }

    // 配置Worker参数
    configureWorkers();

    // 直接调用Worker开始捕获（通过ThreadManager确保在其线程执行）
    const QString threadName = "ScreenCaptureWorker";
    if ( m_threadManager->hasThread(threadName) ) {
        bool startSuccess = m_threadManager->startThread(threadName);
        if ( startSuccess ) {
            // 调用worker的捕获启动方法
            if ( m_captureWorker ) {
                QMetaObject::invokeMethod(m_captureWorker, "startCapturing", Qt::QueuedConnection);
            }
            m_isCapturing.store(true);
            qCInfo(lcServerCapture) << "使用ThreadManager启动ScreenCaptureWorker线程成功";
        } else {
            qCCritical(lcServerCapture) << "ThreadManager启动ScreenCaptureWorker线程失败";
            cleanupThreads();
        }
    } else {
        qCCritical(lcServerCapture) << "ScreenCaptureWorker线程不存在";
        cleanupThreads();
    }
}

void ScreenCapture::stopCapture() {
    const bool wasCapturing = m_isCapturing.exchange(false);
    const QString threadName = QStringLiteral("ScreenCaptureWorker");
    const bool threadExists = m_threadManager && m_threadManager->hasThread(threadName);

    // 若既未在捕获，线程也不存在，则无事可做
    if ( !wasCapturing && !threadExists ) {
        qCDebug(lcServerCapture) << "已停止捕获且线程不存在，忽略停止请求";
        return;
    }

    qCInfo(lcServerCapture) << "停止多线程屏幕捕获 (wasCapturing=" << wasCapturing
        << ", threadExists=" << threadExists << ")";

    // 优先停止队列以唤醒可能阻塞的生产者，确保后续线程停止不会卡住
    if ( m_queueManager ) {
        m_queueManager->stopAllQueues();
    }

    // 通知Worker停止捕获
    // 安全前提：Worker 存在且其线程仍在运行时才使用 BlockingQueuedConnection，
    // 否则目标线程事件循环已退出，BlockingQueuedConnection 会永久阻塞。
    if ( m_captureWorker && m_threadManager && m_threadManager->isThreadRunning(threadName) ) {
        bool invokeSuccess = QMetaObject::invokeMethod(m_captureWorker, "stopCapturing",
            Qt::BlockingQueuedConnection);
        if ( invokeSuccess ) {
            qCInfo(lcServerCapture) << "Worker停止捕获调用成功";
        } else {
            qCWarning(lcServerCapture) << "Worker停止捕获调用失败";
        }
    } else if ( m_captureWorker ) {
        // 线程已停止但 Worker 仍存在，直接设置原子标志
        qCDebug(lcServerCapture) << "Worker线程未运行，直接通知停止";
        m_captureWorker->stopCapturing();
    }

    // 使用ThreadManager停止Worker线程
    if ( threadExists ) {
        bool stopSuccess = m_threadManager->stopThread(threadName, true);
        if ( stopSuccess ) {
            qCInfo(lcServerCapture) << "使用ThreadManager停止ScreenCaptureWorker线程成功";
        } else {
            qCWarning(lcServerCapture) << "ThreadManager停止ScreenCaptureWorker线程失败";
        }
    }

    // 清理线程资源（销毁线程对象，防止 auto-restart 重新启动）
    cleanupThreads();

    qCInfo(lcServerCapture) << "多线程屏幕捕获停止完成";
}

bool ScreenCapture::isCapturing() const {
    return m_isCapturing.load();
}

// 多线程管理方法实现
bool ScreenCapture::initializeThreads() {
    qCInfo(lcServerCapture) << "使用ThreadManager初始化ScreenCaptureWorker线程";

    // 移除：不再创建线程安全队列

    // 通过ThreadManager创建Worker实例
    const QString threadName = "ScreenCaptureWorker";
    if ( m_threadManager->hasThread(threadName) ) {
        qCDebug(lcServerCapture) << "ScreenCaptureWorker线程已存在，先停止并销毁旧线程";
        bool stopped = m_threadManager->stopThread(threadName, true);
        if ( !stopped ) {
            qCWarning(lcServerCapture) << "停止旧ScreenCaptureWorker线程失败，尝试继续销毁";
        }
        bool destroyed = m_threadManager->destroyThread(threadName);
        if ( !destroyed ) {
            qCCritical(lcServerCapture) << "销毁旧ScreenCaptureWorker线程失败，无法重新创建";
            return false;
        }
    }

    // 由ThreadManager创建并持有Worker对象（构造函数已无队列参数）
    bool success = m_threadManager->createThread(
        threadName,
        std::unique_ptr<Worker>(new ScreenCaptureWorker(m_queueManager)),
        false,  // 不自动启动
        true,   // 自动重启
        3       // 最大重启次数
    );

    if ( !success ) {
        qCCritical(lcServerCapture) << "创建ScreenCaptureWorker线程失败";
        return false;
    }

    // 通过ThreadManager获取Worker裸指针并存入QPointer（非拥有）
    Worker* worker = m_threadManager->getWorker(threadName);
    m_captureWorker = qobject_cast<ScreenCaptureWorker*>(worker);
    if ( !m_captureWorker ) {
        qCCritical(lcServerCapture) << "获取ScreenCaptureWorker指针失败";
        return false;
    }

    qCInfo(lcServerCapture) << "ScreenCaptureWorker线程创建成功";
    return true;
}

void ScreenCapture::cleanupThreads() {
    qCInfo(lcServerCapture) << "使用ThreadManager清理ScreenCaptureWorker线程";

    const QString threadName = "ScreenCaptureWorker";
    if ( m_threadManager && m_threadManager->hasThread(threadName) ) {
        bool destroySuccess = m_threadManager->destroyThread(threadName);
        if ( destroySuccess ) {
            qCInfo(lcServerCapture) << "ThreadManager销毁ScreenCaptureWorker线程成功";
        } else {
            qCWarning(lcServerCapture) << "ThreadManager销毁ScreenCaptureWorker线程失败";
        }
    }

    // 仅置空非拥有指针
    m_captureWorker = nullptr;

    qCInfo(lcServerCapture) << "Worker线程清理完成";
}

void ScreenCapture::onThreadStarted(const QString& name) {
    qCInfo(lcServerCapture) << "线程启动: " << name;
    if ( name == "ScreenCaptureWorker" ) {
        Worker* worker = m_threadManager ? m_threadManager->getWorker(name) : nullptr;
        ScreenCaptureWorker* captureWorker = worker ? qobject_cast<ScreenCaptureWorker*>(worker) : nullptr;
        if ( captureWorker ) {
            m_captureWorker = captureWorker; // 更新QPointer
        }
        // 标记捕获状态
        m_isCapturing.store(true);
    }
}

void ScreenCapture::onThreadStopped(const QString& name) {
    qCInfo(lcServerCapture) << "线程停止: " << name;
    if ( name == "ScreenCaptureWorker" ) {
        if ( m_isCapturing.load() ) {
            m_isCapturing.store(false);
            qCWarning(lcServerCapture) << "ScreenCaptureWorker线程意外停止，捕获状态已重置";
        }
        // 注意：不在此处置空 m_captureWorker。
        // 原因：stopCapture() 可能在此信号处理之后运行，仍需要通过
        // m_captureWorker 调用 stopCapturing()。指针在 cleanupThreads() 中统一置空。
        // QPointer 在对象销毁时会自动变为 nullptr，不会产生悬挂指针。
    }
}

void ScreenCapture::onThreadError(const RdError& error) {
    qCCritical(lcServerCapture) << "线程错误 [" << error.source << "]: " << error.message;

    // 如果是ScreenCaptureWorker线程出错，尝试重启
    if ( error.source == "ScreenCaptureWorker" ) {
        qCWarning(lcServerCapture) << "ScreenCaptureWorker线程出错，尝试重启线程";

        // 停止当前捕获
        if ( m_isCapturing.load() ) {
            stopCapture();
        }

        // 清理并重新初始化线程
        cleanupThreads();
        initializeThreads();

        // 如果之前在捕获，重新开始捕获
        if ( !m_isCapturing.load() ) {
            QTimer::singleShot(1000, this, [this]() {
                startCapture();
            });
        }
    }
}

void ScreenCapture::onThreadRestarted(const QString& name, int restartCount) {
    qCDebug(lcServerCapture) << "线程重启 [" << name << "]: 第" << restartCount << "次重启";

    // 如果重启次数过多，停止捕获以避免无限重启
    if ( restartCount > 3 ) {
        qCCritical(lcServerCapture) << "线程 [" << name << "] 重启次数过多，停止捕获";
        if ( m_isCapturing.load() ) {
            stopCapture();
        }
    }
}

void ScreenCapture::configureWorkers() {
    updateCaptureConfig(m_captureConfig);
}


// 统一配置管理方法实现
void ScreenCapture::updateCaptureConfig(const CaptureConfig& config) {
    // 本地归一化配置：对帧率进行边界裁剪，确保对外可见配置始终有效
    const int originalFrameRate = config.frameRate;            // 记录输入帧率（用于日志）
    CaptureConfig normalized = config;
    // 帧率裁剪到平台允许范围
    normalized.frameRate = std::clamp(
        normalized.frameRate,
        CaptureConstants::MinFrameRate,
        CaptureConstants::MaxFrameRate
    );

    {
        QMutexLocker locker(&m_configMutex);
        m_captureConfig = normalized; // 存储归一化后的配置，保证getCaptureConfig可通过边界测试
    }

    // 如果捕获Worker存在，更新其配置（传递归一化后的配置）
    if ( m_captureWorker ) {
        // 直接传递配置，因为现在使用统一的CaptureConfig结构
        m_captureWorker->updateConfig(normalized);
    }

    // 日志增强：同时打印输入值与裁剪后的值，便于问题定位
    qCDebug(lcServerCapture) << "捕获配置已更新: 帧率(输入=" << originalFrameRate
        << ", 裁剪=" << m_captureConfig.frameRate
        << "), 高清=" << (m_captureConfig.highDefinition ? "开启" : "关闭")
        << ", 抗锯齿=" << (m_captureConfig.antiAliasing ? "开启" : "关闭");
}

CaptureConfig ScreenCapture::getCaptureConfig() const {
    QMutexLocker locker(&m_configMutex);
    return m_captureConfig;
}
