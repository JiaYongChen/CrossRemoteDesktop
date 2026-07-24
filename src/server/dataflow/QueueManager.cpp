#include "server/dataflow/QueueManager.h"

#include <QtCore/QMutexLocker>

#include "common/logging/LoggingCategories.h"


QueueManager::QueueManager(QObject* parent)
    : QObject(parent)
    , m_statsTimer(new QTimer(this))
    , m_statsEnabled(true)
    , m_statsUpdateInterval(1000)  // 默认1秒更新一次
    , m_initialized(false) {
    qCDebug(lcServerQueue) << "QueueManager构造函数";

    // 连接统计更新定时器
    connect(m_statsTimer, &QTimer::timeout, this, &QueueManager::updateStats);
}

QueueManager::~QueueManager() {
    qCDebug(lcServerQueue) << "QueueManager析构函数";
    cleanup();
}

bool QueueManager::initialize(int captureQueueSize) {
    qCDebug(lcServerQueue) << "初始化队列管理器，捕获队列大小:" << captureQueueSize;

    if ( m_initialized ) {
        qCDebug(lcServerQueue) << "队列管理器已经初始化";
        return true;
    }

    try {
        // 创建捕获队列
        m_captureQueue = std::make_unique<ThreadSafeQueue<CapturedFrame>>(captureQueueSize);
        if ( !m_captureQueue ) {
            qCCritical(lcServerQueue) << "创建捕获队列失败";
            return false;
        }

        // 初始化统计信息
        {
            QMutexLocker locker(&m_statsMutex);
            m_captureStats = QueueStats();
            m_captureStats.maxSize = captureQueueSize;
        }

        // 启动统计定时器
        if ( m_statsEnabled ) {
            m_statsTimer->start(m_statsUpdateInterval);
        }

        m_initialized = true;
        qCInfo(lcServerQueue) << "队列管理器初始化成功";
        return true;

    } catch ( const std::exception& e ) {
        qCCritical(lcServerQueue) << "初始化队列管理器异常:" << e.what();
        return false;
    } catch ( ... ) {
        qCCritical(lcServerQueue) << "初始化队列管理器未知异常";
        return false;
    }
}

void QueueManager::cleanup() {
    qCDebug(lcServerQueue) << "清理队列管理器";

    // 停止统计定时器
    if ( m_statsTimer && m_statsTimer->isActive() ) {
        m_statsTimer->stop();
    }

    // 停止所有队列
    stopAllQueues();

    // 清理队列
    m_captureQueue.reset();

    m_initialized = false;
    qCInfo(lcServerQueue) << "队列管理器清理完成";
}

QueueStats QueueManager::getQueueStats(QueueType type) const {
    QMutexLocker locker(&m_statsMutex);

    switch ( type ) {
        case CaptureQueue:
            return m_captureStats;
        default:
            qCWarning(lcServerQueue) << "未知的队列类型:" << type;
            return QueueStats();
    }
}

void QueueManager::clearQueue(QueueType type) {
    qCDebug(lcServerQueue) << "清空队列:" << getQueueName(type);

    switch ( type ) {
        case CaptureQueue:
            if ( m_captureQueue ) {
                m_captureQueue->clear();
            }
            break;
        default:
            qCWarning(lcServerQueue) << "清空队列失败，未知类型:" << type;
            break;
    }
}

void QueueManager::stopAllQueues() {
    qCDebug(lcServerQueue) << "停止所有队列（清空）";

    if ( m_captureQueue ) {
        m_captureQueue->clear();
    }

}

void QueueManager::forceUpdateStats() {
    updateStats();
}

void QueueManager::updateStats() {
    if ( !m_initialized ) {
        return;
    }

    updateQueueStats(CaptureQueue);
}

void QueueManager::updateQueueStats(QueueType type) {
    if ( type != CaptureQueue ) {
        return;
    }

    QMutexLocker locker(&m_statsMutex);

    ThreadSafeQueue<CapturedFrame>* captureQueue = m_captureQueue.get();
    if ( !captureQueue ) {
        return;
    }

    // 更新统计信息
    m_captureStats.currentSize = captureQueue->size();
    m_captureStats.totalEnqueued = captureQueue->getTotalEnqueued();
    m_captureStats.totalDequeued = captureQueue->getTotalDequeued();

    // 更新时间戳
    m_captureStats.lastUpdateTime = QDateTime::currentDateTime();

    locker.unlock();
}

QString QueueManager::getQueueName(QueueType type) const {
    switch ( type ) {
        case CaptureQueue:
            return "捕获队列";
        default:
            return "未知队列";
    }
}

// ==================== 捕获队列统一接口实现 ====================

bool QueueManager::enqueueCapturedFrame(const CapturedFrame& frame) {
    if ( !m_captureQueue ) {
        qCWarning(lcServerQueue) << "捕获队列未初始化";
        return false;
    }

    // 如果入队的数据无效，直接返回失败
    if ( !frame.isValid() ) {
        qCWarning(lcServerQueue) << "尝试入队无效的捕获帧，帧ID:" << frame.frameId;
        return false;
    }

    // Drain-to-Latest：队列满时清空所有旧帧，仅保留最新帧。
    // 实时流场景下优先保证低延迟，旧帧的显示价值已被新帧替代。
    const int dropped = m_captureQueue->tryEnqueueDrainToLatest(frame);
    if ( dropped > 0 ) {
        qCDebug(lcServerQueue) << "CaptureQueue drained:" << dropped << "old frames dropped";
    }
    return true;
}

bool QueueManager::dequeueCapturedFrame(CapturedFrame& frame) {
    if ( !m_captureQueue ) {
        qCWarning(lcServerQueue) << "捕获队列未初始化";
        return false;
    }

    // 流水池模型：FIFO 逐帧出队，不排空。
    // 大容量队列(120)在正常运行中保持低位，FIFO 与排空行为无差异；
    // 波动时用容量吸收而非丢弃，保证画面连续性。
    return m_captureQueue->tryDequeue(frame);
}

int QueueManager::getCaptureQueueSize() const {
    return m_captureQueue ? m_captureQueue->size() : 0;
}
