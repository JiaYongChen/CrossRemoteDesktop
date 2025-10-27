#include "ThreadCommunication.h"
#include <QtCore/QMutexLocker>
#include <QtCore/QDebug>
#include <QtCore/QMetaObject>
#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <QtCore/QLoggingCategory> // 使用分类日志
#include "../logging/LoggingCategories.h" // 引入日志分类声明，使用lcThreading分类

// 静态成员初始化
ThreadCommunicationHub* ThreadCommunicationHub::s_instance = nullptr;

ThreadCommunicationHub* ThreadCommunicationHub::instance()
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    
    if (!s_instance) {
        s_instance = new ThreadCommunicationHub();
    }
    
    return s_instance;
}

ThreadCommunicationHub::ThreadCommunicationHub(QObject* parent)
    : QObject(parent)
    , m_cleanupTimer(new QTimer(this))
    , m_maxQueueSize(10000)
    , m_messageCounter(0)
{
    // 初始化统计信息
    m_stats.totalSent = 0;
    m_stats.totalReceived = 0;
    m_stats.totalDropped = 0;
    m_stats.totalErrors = 0;
    m_stats.averageLatency = 0.0;
    m_stats.lastActivity = QDateTime::currentDateTime();
    
    // 设置清理定时器
    m_cleanupTimer->setSingleShot(false);
    m_cleanupTimer->setInterval(60000); // 每分钟清理一次
    connect(m_cleanupTimer, &QTimer::timeout, this, &ThreadCommunicationHub::onCleanupTimer);
    m_cleanupTimer->start();
    
    qCDebug(lcThreading) << "ThreadCommunicationHub initialized";
}

ThreadCommunicationHub::~ThreadCommunicationHub()
{
    qCDebug(lcThreading) << "ThreadCommunicationHub destroying...";
    
    // 停止清理定时器
    if (m_cleanupTimer->isActive()) {
        m_cleanupTimer->stop();
    }
    
    // 清理所有处理器
    QMutexLocker locker(&m_mutex);
    m_handlers.clear();
    m_messageQueue.clear();
    
    qCDebug(lcThreading) << "ThreadCommunicationHub destroyed";
}

bool ThreadCommunicationHub::registerHandler(const QString& threadName, 
                                           std::shared_ptr<IMessageHandler> handler)
{
    if (threadName.isEmpty() || !handler) {
        qCDebug(lcThreading) << "ThreadCommunicationHub::registerHandler() - Invalid parameters";
        return false;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // 检查是否已存在
    if (m_handlers.contains(threadName)) {
        qCDebug(lcThreading) << "ThreadCommunicationHub::registerHandler() - Handler already exists:" << threadName;
        return false;
    }
    
    m_handlers[threadName] = handler;
    
    qCDebug(lcThreading) << "Message handler registered:" << threadName;
    emit handlerRegistered(threadName);
    
    return true;
}

void ThreadCommunicationHub::unregisterHandler(const QString& threadName)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_handlers.remove(threadName) > 0) {
        qCDebug(lcThreading) << "Message handler unregistered:" << threadName;
        emit handlerUnregistered(threadName);
    }
}

bool ThreadCommunicationHub::sendMessage(const ThreadMessage& message)
{
    if (message.sender.isEmpty() || message.receiver.isEmpty()) {
        qCDebug(lcThreading) << "ThreadCommunicationHub::sendMessage() - Invalid sender or receiver";
        return false;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // 检查队列大小
    if (m_messageQueue.size() >= m_maxQueueSize) {
        qCDebug(lcThreading) << "ThreadCommunicationHub::sendMessage() - Message queue full, dropping message";
        m_stats.totalDropped++;
        return false;
    }
    
    // 添加到队列
    m_messageQueue.enqueue(message);
    
    // 更新统计
    updateStats(true);
    
    emit messageSent(message);
    
    // 立即尝试路由消息
    locker.unlock();
    
    // 使用QMetaObject::invokeMethod确保在正确的线程中处理（兼容旧版Qt，调用槽函数）
    QMetaObject::invokeMethod(this, "processMessageQueue", Qt::QueuedConnection);
    
    return true;
}

QString ThreadCommunicationHub::sendCommand(const QString& sender, const QString& receiver,
                                           const QString& command, const QVariant& data,
                                           bool requiresResponse)
{
    ThreadMessage message(sender, receiver, ThreadMessageType::Command, data);
    message.command = command;
    message.requiresResponse = requiresResponse;
    
    if (requiresResponse) {
        message.correlationId = generateMessageId(sender);
    }
    
    if (sendMessage(message)) {
        return message.id;
    }
    
    return QString();
}

QString ThreadCommunicationHub::sendData(const QString& sender, const QString& receiver,
                                        const QVariant& data, int priority)
{
    ThreadMessage message(sender, receiver, ThreadMessageType::Data, data);
    message.priority = qBound(0, priority, 10);
    
    if (sendMessage(message)) {
        return message.id;
    }
    
    return QString();
}

QString ThreadCommunicationHub::sendStatus(const QString& sender, const QString& receiver,
                                          const QVariant& status)
{
    ThreadMessage message(sender, receiver, ThreadMessageType::Status, status);
    
    if (sendMessage(message)) {
        return message.id;
    }
    
    return QString();
}

QString ThreadCommunicationHub::sendError(const QString& sender, const QString& receiver,
                                         const QString& error)
{
    ThreadMessage message(sender, receiver, ThreadMessageType::Error, QVariant(error));
    message.priority = 8; // 错误消息高优先级
    
    if (sendMessage(message)) {
        return message.id;
    }
    
    return QString();
}

void ThreadCommunicationHub::broadcastMessage(const QString& sender, const QVariant& message,
                                             ThreadMessageType type, const QStringList& excludeThreads)
{
    QMutexLocker locker(&m_mutex);
    
    QStringList receivers = m_handlers.keys();
    
    for (const QString& receiver : receivers) {
        if (receiver == sender || excludeThreads.contains(receiver)) {
            continue;
        }
        
        ThreadMessage msg(sender, receiver, type, message);
        
        locker.unlock();
        sendMessage(msg);
        locker.relock();
    }
}

bool ThreadCommunicationHub::sendResponse(const ThreadMessage& originalMessage, 
                                        const QVariant& responseData)
{
    if (originalMessage.correlationId.isEmpty()) {
        qCDebug(lcThreading) << "ThreadCommunicationHub::sendResponse() - Original message has no correlation ID";
        return false;
    }
    
    ThreadMessage response(originalMessage.receiver, originalMessage.sender, 
                          ThreadMessageType::Data, responseData);
    response.correlationId = originalMessage.correlationId;
    
    return sendMessage(response);
}

QStringList ThreadCommunicationHub::getRegisteredThreads() const
{
    QMutexLocker locker(&m_mutex);
    return m_handlers.keys();
}

ThreadCommunicationHub::MessageStats ThreadCommunicationHub::getMessageStats() const
{
    QMutexLocker locker(&m_mutex);
    return m_stats;
}

void ThreadCommunicationHub::cleanupExpiredMessages(qint64 maxAgeMs)
{
    QMutexLocker locker(&m_mutex);
    
    QDateTime cutoffTime = QDateTime::currentDateTime().addMSecs(-maxAgeMs);
    int originalSize = m_messageQueue.size();
    
    // 创建新队列，只保留未过期的消息
    QQueue<ThreadMessage> newQueue;
    
    while (!m_messageQueue.isEmpty()) {
        ThreadMessage msg = m_messageQueue.dequeue();
        if (msg.timestamp > cutoffTime) {
            newQueue.enqueue(msg);
        }
    }
    
    m_messageQueue = newQueue;
    
    int cleanedCount = originalSize - m_messageQueue.size();
    if (cleanedCount > 0) {
        qCDebug(lcThreading) << "Cleaned up" << cleanedCount << "expired messages";
    }
}

void ThreadCommunicationHub::setMaxQueueSize(int maxSize)
{
    QMutexLocker locker(&m_mutex);
    m_maxQueueSize = qMax(100, maxSize); // 最小100条消息
}

int ThreadCommunicationHub::maxQueueSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxQueueSize;
}

void ThreadCommunicationHub::onCleanupTimer()
{
    cleanupExpiredMessages();
}

bool ThreadCommunicationHub::routeMessage(const ThreadMessage& message)
{
    QMutexLocker locker(&m_mutex);
    
    // 查找目标处理器
    auto it = m_handlers.find(message.receiver);
    if (it == m_handlers.end()) {
        qCDebug(lcThreading) << "ThreadCommunicationHub::routeMessage() - No handler found for:" << message.receiver;
        return false;
    }
    
    std::shared_ptr<IMessageHandler> handler = it.value();
    
    locker.unlock();
    
    // 记录开始时间用于计算延迟
    QDateTime startTime = QDateTime::currentDateTime();
    
    bool success = false;
    try {
        success = handler->handleMessage(message);
    } catch (const std::exception& e) {
        qCDebug(lcThreading) << "ThreadCommunicationHub::routeMessage() - Exception in handler:" << e.what();
        success = false;
    } catch (...) {
        qCDebug(lcThreading) << "ThreadCommunicationHub::routeMessage() - Unknown exception in handler";
        success = false;
    }
    
    // 计算延迟
    double latency = startTime.msecsTo(QDateTime::currentDateTime());
    
    // 更新统计
    locker.relock();
    if (success) {
        updateStats(false, latency);
        emit messageReceived(message);
    } else {
        m_stats.totalErrors++;
        emit messageError(message, "Handler returned false");
    }
    
    return success;
}

QString ThreadCommunicationHub::generateMessageId(const QString& sender)
{
    m_messageCounter++;
    return QString("%1_%2_%3")
           .arg(sender)
           .arg(QDateTime::currentMSecsSinceEpoch())
           .arg(reinterpret_cast<quintptr>(QThread::currentThread()));
}

void ThreadCommunicationHub::updateStats(bool sent, double latency)
{
    m_stats.lastActivity = QDateTime::currentDateTime();
    
    if (sent) {
        m_stats.totalSent++;
    } else {
        m_stats.totalReceived++;
        
        // 更新平均延迟
        if (m_stats.totalReceived == 1) {
            m_stats.averageLatency = latency;
        } else {
            // 使用指数移动平均
            double alpha = 0.1; // 平滑因子
            m_stats.averageLatency = alpha * latency + (1.0 - alpha) * m_stats.averageLatency;
        }
    }
}
void ThreadCommunicationHub::processMessageQueue()
{
    QMutexLocker locker(&m_mutex);

    while (!m_messageQueue.isEmpty()) {
        ThreadMessage msg = m_messageQueue.dequeue();
        locker.unlock();

        if (!routeMessage(msg)) {
            QMutexLocker relocker(&m_mutex);
            m_stats.totalErrors++;
            emit messageError(msg, "Failed to route message");
        }

        locker.relock();
    }
}