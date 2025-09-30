#include "ScreenCapture.h"
#include "ScreenCaptureWorker.h"
// 移除：不再需要线程安全队列头文件
// #include "../../common/core/threading/ThreadSafeQueue.h"
#include "../../common/core/threading/ThreadManager.h"
#include "../../common/core/config/Constants.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtGui/QScreen>
#include <QtGui/QGuiApplication>
#include <memory>
#include <QtCore/QLoggingCategory>
// 新增头文件: 使用std::clamp进行数值裁剪
#include <algorithm>

// 日志分类
Q_LOGGING_CATEGORY(screenCaptureManager, "screencapture.manager")

ScreenCapture::ScreenCapture(QObject *parent)
    : QObject(parent)
    , m_threadManager(ThreadManager::instance())
    , m_isCapturing(false)
    , m_statsTimer(new QTimer(this))
{
    qCDebug(screenCaptureManager, "ScreenCapture 多线程管理器构造函数调用");
    
    // 初始化默认配置
    m_captureConfig.frameRate = CoreConstants::Capture::DEFAULT_FRAME_RATE;
    m_captureConfig.quality = CoreConstants::Capture::DEFAULT_CAPTURE_QUALITY;
    m_captureConfig.highDefinition = true;
    m_captureConfig.antiAliasing = true;

    m_captureConfig.highScaleQuality = true;
    m_captureConfig.captureRect = QRect(); // 空矩形表示全屏
    
    // 初始化性能统计
    resetPerformanceStats();
    
    // 设置统计更新定时器
    m_statsTimer->setInterval(STATS_UPDATE_INTERVAL);
    connect(m_statsTimer, &QTimer::timeout, this, &ScreenCapture::updatePerformanceStats);
    
    // 连接ThreadManager信号以监控线程状态
    connect(m_threadManager, &ThreadManager::threadStarted, this, &ScreenCapture::onThreadStarted);
    connect(m_threadManager, &ThreadManager::threadStopped, this, &ScreenCapture::onThreadStopped);
    connect(m_threadManager, &ThreadManager::threadError, this, &ScreenCapture::onThreadError);
    connect(m_threadManager, &ThreadManager::threadRestarted, this, &ScreenCapture::onThreadRestarted);
    
    qCDebug(screenCaptureManager, "ScreenCapture 多线程管理器构造完成");
}

ScreenCapture::~ScreenCapture()
{
    qCDebug(screenCaptureManager, "ScreenCapture 多线程管理器析构函数调用");
    
    // 停止捕获
    stopCapture();
    
    // 清理线程资源
    cleanupThreads();
    
    qCDebug(screenCaptureManager, "ScreenCapture 多线程管理器析构完成");
}

void ScreenCapture::startCapture()
{
    if (m_isCapturing.load()) {
        qCDebug(screenCaptureManager, "已在捕获中，忽略启动请求");
        return;
    }
    
    int currentFrameRate;
    double currentQuality;
    {
        QMutexLocker locker(&m_configMutex);
        currentFrameRate = m_captureConfig.frameRate;
        currentQuality = m_captureConfig.quality;
    }
    qCInfo(screenCaptureManager, "启动多线程屏幕捕获，帧率: %d, 质量: %.2f", 
           currentFrameRate, currentQuality);
    
    // 初始化线程架构
    if (!initializeThreads()) {
        qCCritical(screenCaptureManager, "线程初始化失败，无法启动捕获");
        emit captureError("线程初始化失败");
        return;
    }
    
    // 配置Worker参数
    configureWorkers();
    
    // 直接调用Worker开始捕获（通过ThreadManager确保在其线程执行）
    const QString threadName = "ScreenCaptureWorker";
    if (m_threadManager->hasThread(threadName)) {
        bool startSuccess = m_threadManager->startThread(threadName);
        if (startSuccess) {
            // 在线程启动后，连接worker信号到本类信号/槽
            if (m_captureWorker) {
                // 帧信号：直接转发为frameReady
                connect(m_captureWorker, &ScreenCaptureWorker::frameCaptured, this,
                        [this](const QImage& frame, qint64 /*timestamp*/) {
                            emit frameReady(frame);
                        });
                // 统计信号：将CaptureStats映射到PerformanceStats（不再计算队列使用率）
                connect(m_captureWorker, &ScreenCaptureWorker::captureStatsUpdated, this,
                        [this](const CaptureStats& stats) {
                            QMutexLocker locker(&m_statsMutex);
                            m_performanceStats.captureFrameRate = stats.currentFrameRate;
                            m_performanceStats.totalFramesCaptured = stats.totalFramesCaptured;
                            m_performanceStats.droppedFrames = stats.droppedFrames;
                            m_performanceStats.averageCaptureTime = static_cast<quint64>(stats.avgCaptureTime.count());
                            // 队列已移除，直接发出统计更新
                            emit performanceStatsUpdated(m_performanceStats);
                        });
            }
            // 调用worker的捕获启动方法
            if (m_captureWorker) {
                QMetaObject::invokeMethod(m_captureWorker, "startCapturing", Qt::QueuedConnection);
            }
            m_isCapturing.store(true);
            m_statsTimer->start();
            qCInfo(screenCaptureManager, "使用ThreadManager启动ScreenCaptureWorker线程成功，已连接直接信号");
        } else {
            qCCritical(screenCaptureManager, "ThreadManager启动ScreenCaptureWorker线程失败");
            emit captureError("线程启动失败");
            cleanupThreads();
        }
    } else {
        qCCritical(screenCaptureManager, "ScreenCaptureWorker线程不存在");
        emit captureError("Worker线程不存在");
        cleanupThreads();
    }
}

void ScreenCapture::stopCapture()
{
    if (!m_isCapturing.load()) {
        qCDebug(screenCaptureManager, "已停止捕获，忽略停止请求");
        return;
    }
    
    qCInfo(screenCaptureManager, "停止多线程屏幕捕获");
    
    // 设置停止标志
    m_isCapturing.store(false);
    
    // 停止统计定时器
    m_statsTimer->stop();
    
    // 通知Worker停止捕获
    if (m_captureWorker) {
        QMetaObject::invokeMethod(m_captureWorker, "stopCapturing", Qt::QueuedConnection);
    }
    
    // 使用ThreadManager停止Worker线程
    const QString threadName = "ScreenCaptureWorker";
    if (m_threadManager->hasThread(threadName)) {
        bool stopSuccess = m_threadManager->stopThread(threadName, true); // 等待当前任务完成
        if (stopSuccess) {
            qCInfo(screenCaptureManager, "使用ThreadManager停止ScreenCaptureWorker线程成功");
        } else {
            qCWarning(screenCaptureManager, "ThreadManager停止ScreenCaptureWorker线程失败");
        }
    }
    
    // 清理线程资源
    cleanupThreads();
    
    qCInfo(screenCaptureManager, "多线程屏幕捕获停止完成");
}

bool ScreenCapture::isCapturing() const
{
    return m_isCapturing.load();
}

// 多线程管理方法实现
bool ScreenCapture::initializeThreads()
{
    qCInfo(screenCaptureManager, "使用ThreadManager初始化ScreenCaptureWorker线程");
    
    // 移除：不再创建线程安全队列
    
    // 通过ThreadManager创建Worker实例
    const QString threadName = "ScreenCaptureWorker";
    if (m_threadManager->hasThread(threadName)) {
        qCWarning(screenCaptureManager, "ScreenCaptureWorker线程已存在，先销毁旧线程");
        m_threadManager->destroyThread(threadName);
    }
    
    // 由ThreadManager创建并持有Worker对象（构造函数已无队列参数）
    bool success = m_threadManager->createThread(
        threadName,
        std::unique_ptr<Worker>(new ScreenCaptureWorker(nullptr)),
        false,  // 不自动启动
        true,   // 自动重启
        3       // 最大重启次数
    );
    
    if (!success) {
        qCCritical(screenCaptureManager, "创建ScreenCaptureWorker线程失败");
        return false;
    }
    
    // 通过ThreadManager获取Worker裸指针并存入QPointer（非拥有）
    Worker* worker = m_threadManager->getWorker(threadName);
    m_captureWorker = qobject_cast<ScreenCaptureWorker*>(worker);
    if (!m_captureWorker) {
        qCCritical(screenCaptureManager, "获取ScreenCaptureWorker指针失败");
        return false;
    }
    
    // 连接Worker错误信号至ScreenCapture错误处理
    connect(m_captureWorker, &Worker::errorOccurred, this, &ScreenCapture::onCaptureError);
    
    qCInfo(screenCaptureManager, "ScreenCaptureWorker线程创建成功");
    return true;
}

void ScreenCapture::cleanupThreads()
{
    qCInfo(screenCaptureManager, "使用ThreadManager清理ScreenCaptureWorker线程");
    
    const QString threadName = "ScreenCaptureWorker";
    if (m_threadManager && m_threadManager->hasThread(threadName)) {
        bool destroySuccess = m_threadManager->destroyThread(threadName);
        if (destroySuccess) {
            qCInfo(screenCaptureManager, "ThreadManager销毁ScreenCaptureWorker线程成功");
        } else {
            qCWarning(screenCaptureManager, "ThreadManager销毁ScreenCaptureWorker线程失败");
        }
    }
    
    // 仅置空非拥有指针
    m_captureWorker = nullptr;
    
    qCInfo(screenCaptureManager, "Worker线程清理完成");
}

void ScreenCapture::onThreadStarted(const QString& name)
{
    qCInfo(screenCaptureManager, "线程启动: %s", qPrintable(name));
    if (name == "ScreenCaptureWorker") {
        Worker* worker = m_threadManager ? m_threadManager->getWorker(name) : nullptr;
        ScreenCaptureWorker* captureWorker = worker ? qobject_cast<ScreenCaptureWorker*>(worker) : nullptr;
        if (captureWorker) {
            m_captureWorker = captureWorker; // 更新QPointer
        }
        // 标记捕获状态
        m_isCapturing.store(true);
    }
}

void ScreenCapture::onThreadStopped(const QString& name)
{
    qCInfo(screenCaptureManager, "线程停止: %s", qPrintable(name));
    if (name == "ScreenCaptureWorker") {
        if (m_isCapturing.load()) {
            m_isCapturing.store(false);
            qCWarning(screenCaptureManager, "ScreenCaptureWorker线程意外停止，捕获状态已重置");
        }
        // 线程停止后将指针置空，避免悬挂
        m_captureWorker = nullptr;
    }
}

void ScreenCapture::onThreadError(const QString& name, const QString& error)
{
    qCCritical(screenCaptureManager, "线程错误 [%s]: %s", qPrintable(name), qPrintable(error));
    
    // 如果是ScreenCaptureWorker线程出错，尝试重启
    if (name == "ScreenCaptureWorker") {
        qCWarning(screenCaptureManager, "ScreenCaptureWorker线程出错，尝试重启线程");
        
        // 停止当前捕获
        if (m_isCapturing.load()) {
            stopCapture();
        }
        
        // 清理并重新初始化线程
        cleanupThreads();
        initializeThreads();
        
        // 如果之前在捕获，重新开始捕获
        if (!m_isCapturing.load()) {
            QTimer::singleShot(1000, this, [this]() {
                startCapture();
            });
        }
    }
}

void ScreenCapture::onThreadRestarted(const QString& name, int restartCount)
{
    qCWarning(screenCaptureManager, "线程重启 [%s]: 第%d次重启", qPrintable(name), restartCount);
    
    // 如果重启次数过多，停止捕获以避免无限重启
    if (restartCount > 3) {
        qCCritical(screenCaptureManager, "线程 [%s] 重启次数过多，停止捕获", qPrintable(name));
        if (m_isCapturing.load()) {
            stopCapture();
        }
    }
}

void ScreenCapture::configureWorkers()
{
    updateCaptureConfig(m_captureConfig);
}

void ScreenCapture::updatePerformanceStats()
{
    if (!m_isCapturing.load()) {
        return;
    }
    
    // 更新统计信息
    if (m_captureWorker) {
        qCDebug(screenCaptureManager, "捕获Worker状态正常");
    }
    
    // 移除：队列状态检查与告警
}

void ScreenCapture::resetPerformanceStats()
{
    // 重置性能统计数据
    qCDebug(screenCaptureManager, "重置性能统计数据");
}

ScreenCapture::PerformanceStats ScreenCapture::getPerformanceStats() const
{
    QMutexLocker locker(&m_statsMutex);
    PerformanceStats stats = m_performanceStats;
    // 移除：队列使用率计算
    return stats;
}


void ScreenCapture::onCaptureError(const QString& error)
{
    // 处理捕获错误
    qCWarning(screenCaptureManager, "捕获错误: %s", error.toUtf8().constData());
    emit captureError(error);
}

// 统一配置管理方法实现
void ScreenCapture::updateCaptureConfig(const CaptureConfig& config)
{
    // 本地归一化配置：对帧率与质量进行边界裁剪，确保对外可见配置始终有效
    const int originalFrameRate = config.frameRate;            // 记录输入帧率（用于日志）
    const double originalQuality = config.quality;             // 记录输入质量（用于日志）
    CaptureConfig normalized = config;
    // 帧率裁剪到平台允许范围
    normalized.frameRate = std::clamp(
        normalized.frameRate,
        CoreConstants::Capture::MIN_FRAME_RATE,
        CoreConstants::Capture::MAX_FRAME_RATE
    );
    // 质量裁剪到[0.0, 1.0]
    if (normalized.quality < 0.0) normalized.quality = 0.0;
    if (normalized.quality > 1.0) normalized.quality = 1.0;

    {
        QMutexLocker locker(&m_configMutex);
        m_captureConfig = normalized; // 存储归一化后的配置，保证getCaptureConfig可通过边界测试
    }
    
    // 如果捕获Worker存在，更新其配置（传递归一化后的配置）
    if (m_captureWorker) {
        // 直接传递配置，因为现在使用统一的CaptureConfig结构
        m_captureWorker->updateConfig(normalized);
    }
    
    // 日志增强：同时打印输入值与裁剪后的值，便于问题定位
    qCInfo(screenCaptureManager,
           "捕获配置已更新: 帧率(输入=%d, 裁剪=%d), 质量(输入=%.2f, 裁剪=%.2f), 高清=%s, 抗锯齿=%s",
           originalFrameRate, m_captureConfig.frameRate,
           originalQuality, m_captureConfig.quality,
           m_captureConfig.highDefinition ? "开启" : "关闭",
           m_captureConfig.antiAliasing ? "开启" : "关闭");
}

CaptureConfig ScreenCapture::getCaptureConfig() const
{
    QMutexLocker locker(&m_configMutex);
    return m_captureConfig;
}