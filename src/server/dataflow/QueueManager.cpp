#include "QueueManager.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QMutexLocker>


// 静态成员初始化
QueueManager* QueueManager::s_instance = nullptr;
QMutex QueueManager::s_instanceMutex;

QueueManager::QueueManager(QObject* parent)
    : QObject(parent)
    , m_statsTimer(new QTimer(this))
    , m_statsEnabled(true)
    , m_statsUpdateInterval(1000)  // 默认1秒更新一次
    , m_initialized(false)
    , m_lastProcessedFrameId(0) {
    qCDebug(lcQueueManager) << "QueueManager构造函数";

    // 连接统计更新定时器
    connect(m_statsTimer, &QTimer::timeout, this, &QueueManager::updateStats);
}

QueueManager::~QueueManager() {
    qCDebug(lcQueueManager) << "QueueManager析构函数";
    cleanup();
}

QueueManager* QueueManager::instance() {
    QMutexLocker locker(&s_instanceMutex);
    if ( !s_instance ) {
        s_instance = new QueueManager();
    }
    return s_instance;
}

bool QueueManager::initialize(int captureQueueSize, int processedQueueSize) {
    qCDebug(lcQueueManager) << "初始化队列管理器，捕获队列大小:" << captureQueueSize
        << "处理队列大小:" << processedQueueSize;

    if ( m_initialized ) {
        qCWarning(lcQueueManager) << "队列管理器已经初始化";
        return true;
    }

    try {
        // 创建捕获队列
        m_captureQueue = std::make_unique<ThreadSafeQueue<CapturedFrame>>(captureQueueSize);
        if ( !m_captureQueue ) {
            qCCritical(lcQueueManager) << "创建捕获队列失败";
            return false;
        }

        // 创建处理队列
        m_processedQueue = std::make_unique<ThreadSafeQueue<ProcessedData>>(processedQueueSize);
        if ( !m_processedQueue ) {
            qCCritical(lcQueueManager) << "创建处理队列失败";
            return false;
        }

        // 初始化统计信息
        {
            QMutexLocker locker(&m_statsMutex);
            m_captureStats = QueueStats();
            m_captureStats.maxSize = captureQueueSize;

            m_processedStats = QueueStats();
            m_processedStats.maxSize = processedQueueSize;
        }

        // 启动统计定时器
        if ( m_statsEnabled ) {
            m_statsTimer->start(m_statsUpdateInterval);
        }

        m_initialized = true;
        qCDebug(lcQueueManager) << "队列管理器初始化成功";
        return true;

    } catch ( const std::exception& e ) {
        qCCritical(lcQueueManager) << "初始化队列管理器异常:" << e.what();
        return false;
    } catch ( ... ) {
        qCCritical(lcQueueManager) << "初始化队列管理器未知异常";
        return false;
    }
}

void QueueManager::cleanup() {
    qCDebug(lcQueueManager) << "清理队列管理器";

    // 停止统计定时器
    if ( m_statsTimer && m_statsTimer->isActive() ) {
        m_statsTimer->stop();
    }

    // 停止所有队列
    stopAllQueues();

    // 清理队列
    m_captureQueue.reset();
    m_processedQueue.reset();

    // 重置最后入队的帧ID
    m_lastProcessedFrameId = 0;

    m_initialized = false;
    qCDebug(lcQueueManager) << "队列管理器清理完成";
}

QueueStats QueueManager::getQueueStats(QueueType type) const {
    QMutexLocker locker(&m_statsMutex);

    switch ( type ) {
        case CaptureQueue:
            return m_captureStats;
        case ProcessedQueue:
            return m_processedStats;
        default:
            qCWarning(lcQueueManager) << "未知的队列类型:" << type;
            return QueueStats();
    }
}

void QueueManager::setQueueMaxSize(QueueType type, int maxSize) {
    qCDebug(lcQueueManager) << "设置队列最大大小，类型:" << getQueueName(type) << "大小:" << maxSize;

    switch ( type ) {
        case CaptureQueue:
            if ( m_captureQueue ) {
                m_captureQueue->setMaxSize(maxSize);
                QMutexLocker locker(&m_statsMutex);
                m_captureStats.maxSize = maxSize;
            }
            break;
        case ProcessedQueue:
            if ( m_processedQueue ) {
                m_processedQueue->setMaxSize(maxSize);
                QMutexLocker locker(&m_statsMutex);
                m_processedStats.maxSize = maxSize;
            }
            break;
        default:
            qCWarning(lcQueueManager) << "设置队列大小失败，未知类型:" << type;
            break;
    }
}

void QueueManager::clearQueue(QueueType type) {
    qCDebug(lcQueueManager) << "清空队列:" << getQueueName(type);

    switch ( type ) {
        case CaptureQueue:
            if ( m_captureQueue ) {
                m_captureQueue->clear();
            }
            break;
        case ProcessedQueue:
            if ( m_processedQueue ) {
                m_processedQueue->clear();
            }
            // 重置最后入队的帧ID
            m_lastProcessedFrameId = 0;
            break;
        default:
            qCWarning(lcQueueManager) << "清空队列失败，未知类型:" << type;
            break;
    }
}

void QueueManager::stopAllQueues() {
    qCDebug(lcQueueManager) << "停止所有队列";

    if ( m_captureQueue ) {
        m_captureQueue->stop();
    }

    if ( m_processedQueue ) {
        m_processedQueue->stop();
    }
}

void QueueManager::restartAllQueues() {
    qCDebug(lcQueueManager) << "重启所有队列";

    if ( m_captureQueue ) {
        m_captureQueue->restart();
    }

    if ( m_processedQueue ) {
        m_processedQueue->restart();
    }

    // 重置最后入队的帧ID
    m_lastProcessedFrameId = 0;
}

bool QueueManager::isQueueHealthy(QueueType type) const {
    QueueStats stats = getQueueStats(type);

    // 检查队列使用率
    double usage = stats.getUsagePercentage();
    if ( usage > QUEUE_ERROR_THRESHOLD ) {
        return false;
    }

    // 检查平均延迟
    if ( stats.averageLatency > MAX_LATENCY_WARNING ) {
        return false;
    }

    return true;
}

void QueueManager::setStatsEnabled(bool enabled) {
    qCDebug(lcQueueManager) << "设置统计启用状态:" << enabled;

    m_statsEnabled = enabled;

    if ( enabled && m_initialized ) {
        if ( !m_statsTimer->isActive() ) {
            m_statsTimer->start(m_statsUpdateInterval);
        }
    } else {
        if ( m_statsTimer->isActive() ) {
            m_statsTimer->stop();
        }
    }
}

void QueueManager::setStatsUpdateInterval(int intervalMs) {
    qCDebug(lcQueueManager) << "设置统计更新间隔:" << intervalMs << "毫秒";

    m_statsUpdateInterval = intervalMs;

    if ( m_statsTimer->isActive() ) {
        m_statsTimer->stop();
        m_statsTimer->start(m_statsUpdateInterval);
    }
}

void QueueManager::forceUpdateStats() {
    qCDebug(lcQueueManager) << "强制更新统计信息";
    updateStats();
}

void QueueManager::updateStats() {
    if ( !m_initialized ) {
        return;
    }

    updateQueueStats(CaptureQueue);
    updateQueueStats(ProcessedQueue);

    // 检查队列健康状态
    checkQueueHealth(CaptureQueue);
    checkQueueHealth(ProcessedQueue);
}

void QueueManager::updateQueueStats(QueueType type) {
    QMutexLocker locker(&m_statsMutex);

    QueueStats* stats = nullptr;
    ThreadSafeQueue<CapturedFrame>* captureQueue = nullptr;
    ThreadSafeQueue<ProcessedData>* processedQueue = nullptr;

    switch ( type ) {
        case CaptureQueue:
            stats = &m_captureStats;
            captureQueue = m_captureQueue.get();
            break;
        case ProcessedQueue:
            stats = &m_processedStats;
            processedQueue = m_processedQueue.get();
            break;
        default:
            return;
    }

    if ( !stats ) {
        return;
    }

    // 更新统计信息
    if ( captureQueue ) {
        stats->currentSize = captureQueue->size();
        stats->totalEnqueued = captureQueue->getTotalEnqueued();
        stats->totalDequeued = captureQueue->getTotalDequeued();
    } else if ( processedQueue ) {
        stats->currentSize = processedQueue->size();
        stats->totalEnqueued = processedQueue->getTotalEnqueued();
        stats->totalDequeued = processedQueue->getTotalDequeued();
    }

    // 更新时间戳
    stats->lastUpdateTime = QDateTime::currentDateTime();

    // 发射统计更新信号
    locker.unlock();
    emit queueStatsUpdated(type, *stats);
}

void QueueManager::checkQueueHealth(QueueType type) {
    QueueStats stats = getQueueStats(type);
    QString queueName = getQueueName(type);

    double usage = stats.getUsagePercentage();

    // 检查队列使用率警告
    if ( usage > QUEUE_ERROR_THRESHOLD ) {
        QString error = QString("队列 %1 使用率过高: %2%").arg(queueName).arg(usage, 0, 'f', 1);
        emit queueError(type, error);
    } else if ( usage > QUEUE_WARNING_THRESHOLD ) {
        QString warning = QString("队列 %1 使用率较高: %2%").arg(queueName).arg(usage, 0, 'f', 1);
        emit queueWarning(type, warning);
    }

    // 检查延迟警告
    if ( stats.averageLatency > MAX_LATENCY_WARNING ) {
        QString warning = QString("队列 %1 平均延迟过高: %2ms").arg(queueName).arg(stats.averageLatency, 0, 'f', 1);
        emit queueWarning(type, warning);
    }
}

QString QueueManager::getQueueName(QueueType type) const {
    switch ( type ) {
        case CaptureQueue:
            return "捕获队列";
        case ProcessedQueue:
            return "处理队列";
        default:
            return "未知队列";
    }
}

// ==================== 捕获队列统一接口实现 ====================

bool QueueManager::enqueueCapturedFrame(const CapturedFrame& frame) {
    if ( !m_captureQueue ) {
        qCWarning(lcQueueManager) << "捕获队列未初始化";
        return false;
    }

    // 如果入队的数据无效，直接返回失败
    if ( !frame.isValid() ) {
        qCWarning(lcQueueManager) << "尝试入队无效的捕获帧，帧ID:" << frame.frameId;
        return false;
    }

    // 非阻塞入队：队列满时丢弃新帧（不弹旧帧）。
    // DXGI 不缓冲历史帧，CQ 满时背压无意义——GPU 侧帧已丢失。
    // 大容量(120)使 CQ 满仅发生在极端编码落后场景，正常波动不影响。
    return m_captureQueue->tryEnqueue(frame);
}

bool QueueManager::dequeueCapturedFrame(CapturedFrame& frame) {
    if ( !m_captureQueue ) {
        qCWarning(lcQueueManager) << "捕获队列未初始化";
        return false;
    }

    // 流水池模型：FIFO 逐帧出队，不排空。
    // 大容量队列(120)在正常运行中保持低位，FIFO 与排空行为无差异；
    // 波动时用容量吸收而非丢弃，保证画面连续性。
    return m_captureQueue->tryDequeue(frame);
}

// ==================== 处理队列统一接口实现 ====================

bool QueueManager::enqueueProcessedData(const ProcessedData& data) {
    if ( !m_processedQueue ) {
        qCWarning(lcQueueManager) << "处理队列未初始化";
        return false;
    }

    // 如果入队的数据无效，直接返回失败
    if ( !data.isValid() ) {
        qCWarning(lcQueueManager) << "尝试入队无效的处理数据，帧ID:" << data.originalFrameId;
        return false;
    }

    // 非阻塞入队：队列满时返回 false，调用方自行决定丢弃或重试
    return m_processedQueue->tryEnqueue(data);
}

bool QueueManager::dequeueProcessedData(ProcessedData& data) {
    if ( !m_processedQueue ) {
        qCWarning(lcQueueManager) << "处理队列未初始化";
        return false;
    }

    // 流水池模型：FIFO 逐帧出队，不排空。
    // 发送端每轮取 1-6 帧(MAX_SEND_BATCH)，逐帧发送保证画面连续性。
    return m_processedQueue->tryDequeue(data);
}

bool QueueManager::isProcessedQueueFull() const {
    return m_processedQueue && m_processedQueue->isFull();
}

int QueueManager::getProcessedQueueSize() const {
    return m_processedQueue ? m_processedQueue->size() : 0;
}

int QueueManager::getCaptureQueueSize() const {
    return m_captureQueue ? m_captureQueue->size() : 0;
}

int QueueManager::getProcessedQueueMaxSize() const {
    return m_processedQueue ? m_processedQueue->maxSize() : 0;
}
