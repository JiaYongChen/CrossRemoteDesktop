#ifndef THREADCOMMUNICATION_H
#define THREADCOMMUNICATION_H

#include <QtCore/QObject>
#include <QtCore/QVariant>
#include <QtCore/QMetaType>
#include <QtCore/QMutex>
#include <QtCore/QWaitCondition>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QQueue>
#include <QtCore/QThread>
#include <memory>
#include <functional>

/**
 * @brief 线程间消息类型
 */
enum class ThreadMessageType : int {
    Command = 0,    // 命令消息
    Data = 1,       // 数据消息
    Status = 2,     // 状态消息
    Error = 3,      // 错误消息
    Heartbeat = 4,  // 心跳消息
    Custom = 5      // 自定义消息
};

/**
 * @brief 线程间消息结构
 */
struct ThreadMessage {
    QString id;                    // 消息ID
    QString sender;                // 发送者线程名称
    QString receiver;              // 接收者线程名称
    ThreadMessageType type;        // 消息类型
    QString command;               // 命令名称（当type为Command时使用）
    QVariant data;                 // 消息数据
    QDateTime timestamp;           // 时间戳
    int priority;                  // 优先级（0-10，数字越大优先级越高）
    bool requiresResponse;         // 是否需要响应
    QString correlationId;         // 关联ID（用于请求-响应配对）
    
    ThreadMessage()
        : type(ThreadMessageType::Data)
        , timestamp(QDateTime::currentDateTime())
        , priority(5)
        , requiresResponse(false)
    {
    }
    
    ThreadMessage(const QString& senderId, const QString& receiverId, 
                 ThreadMessageType msgType, const QVariant& msgData)
        : sender(senderId)
        , receiver(receiverId)
        , type(msgType)
        , data(msgData)
        , timestamp(QDateTime::currentDateTime())
        , priority(5)
        , requiresResponse(false)
    {
        // 生成唯一ID，使用线程指针避免QRandomGenerator多线程问题
        id = QString("%1_%2_%3").arg(sender).arg(timestamp.toMSecsSinceEpoch()).arg(reinterpret_cast<quintptr>(QThread::currentThread()));
    }
};

/**
 * @brief 消息处理器接口
 */
class IMessageHandler
{
public:
    virtual ~IMessageHandler() = default;
    
    /**
     * @brief 处理接收到的消息
     * @param message 消息对象
     * @return 是否处理成功
     */
    virtual bool handleMessage(const ThreadMessage& message) = 0;
    
    /**
     * @brief 获取处理器名称
     * @return 处理器名称
     */
    virtual QString handlerName() const = 0;
};

/**
 * @brief 线程通信中心
 * 负责管理线程间的消息传递和路由
 */
class ThreadCommunicationHub : public QObject
{
    Q_OBJECT
    
public:
    /**
     * @brief 获取单例实例
     * @return 通信中心实例
     */
    static ThreadCommunicationHub* instance();
    
    /**
     * @brief 注册消息处理器
     * @param threadName 线程名称
     * @param handler 消息处理器
     * @return 是否注册成功
     */
    bool registerHandler(const QString& threadName, std::shared_ptr<IMessageHandler> handler);
    
    /**
     * @brief 注销消息处理器
     * @param threadName 线程名称
     */
    void unregisterHandler(const QString& threadName);
    
    /**
     * @brief 发送消息
     * @param message 消息对象
     * @return 是否发送成功
     */
    bool sendMessage(const ThreadMessage& message);
    
    /**
     * @brief 发送命令消息
     * @param sender 发送者
     * @param receiver 接收者
     * @param command 命令名称
     * @param data 命令数据
     * @param requiresResponse 是否需要响应
     * @return 消息ID
     */
    QString sendCommand(const QString& sender, const QString& receiver,
                       const QString& command, const QVariant& data = QVariant(),
                       bool requiresResponse = false);
    
    /**
     * @brief 发送数据消息
     * @param sender 发送者
     * @param receiver 接收者
     * @param data 数据
     * @param priority 优先级
     * @return 消息ID
     */
    QString sendData(const QString& sender, const QString& receiver,
                    const QVariant& data, int priority = 5);
    
    /**
     * @brief 发送状态消息
     * @param sender 发送者
     * @param receiver 接收者
     * @param status 状态信息
     * @return 消息ID
     */
    QString sendStatus(const QString& sender, const QString& receiver,
                      const QVariant& status);
    
    /**
     * @brief 发送错误消息
     * @param sender 发送者
     * @param receiver 接收者
     * @param error 错误信息
     * @return 消息ID
     */
    QString sendError(const QString& sender, const QString& receiver,
                     const QString& error);
    
    /**
     * @brief 广播消息
     * @param sender 发送者
     * @param message 消息内容
     * @param type 消息类型
     * @param excludeThreads 排除的线程列表
     */
    void broadcastMessage(const QString& sender, const QVariant& message,
                         ThreadMessageType type = ThreadMessageType::Data,
                         const QStringList& excludeThreads = QStringList());
    
    /**
     * @brief 发送响应消息
     * @param originalMessage 原始消息
     * @param responseData 响应数据
     * @return 是否发送成功
     */
    bool sendResponse(const ThreadMessage& originalMessage, const QVariant& responseData);
    
    /**
     * @brief 获取已注册的线程列表
     * @return 线程名称列表
     */
    QStringList getRegisteredThreads() const;
    
    /**
     * @brief 获取消息统计信息
     * @return 统计信息
     */
    struct MessageStats {
        quint64 totalSent;          // 总发送数
        quint64 totalReceived;      // 总接收数
        quint64 totalDropped;       // 总丢弃数
        quint64 totalErrors;        // 总错误数
        double averageLatency;      // 平均延迟（毫秒）
        QDateTime lastActivity;     // 最后活动时间
    };
    MessageStats getMessageStats() const;
    
    /**
     * @brief 清理过期消息
     * @param maxAgeMs 最大保留时间（毫秒）
     */
    void cleanupExpiredMessages(qint64 maxAgeMs = 300000); // 默认5分钟
    
    /**
     * @brief 设置消息队列最大大小
     * @param maxSize 最大大小
     */
    void setMaxQueueSize(int maxSize);
    
    /**
     * @brief 获取消息队列最大大小
     * @return 最大大小
     */
    int maxQueueSize() const;
    
signals:
    /**
     * @brief 消息发送信号
     * @param message 消息对象
     */
    void messageSent(const ThreadMessage& message);
    
    /**
     * @brief 消息接收信号
     * @param message 消息对象
     */
    void messageReceived(const ThreadMessage& message);
    
    /**
     * @brief 消息处理错误信号
     * @param message 消息对象
     * @param error 错误信息
     */
    void messageError(const ThreadMessage& message, const QString& error);
    
    /**
     * @brief 处理器注册信号
     * @param threadName 线程名称
     */
    void handlerRegistered(const QString& threadName);
    
    /**
     * @brief 处理器注销信号
     * @param threadName 线程名称
     */
    void handlerUnregistered(const QString& threadName);
    
private slots:
    /**
     * @brief 清理定时器槽函数
     */
    void onCleanupTimer();
    
private:
    explicit ThreadCommunicationHub(QObject* parent = nullptr);
    ~ThreadCommunicationHub();
    
    /**
     * @brief 路由消息到目标处理器
     * @param message 消息对象
     * @return 是否路由成功
     */
    bool routeMessage(const ThreadMessage& message);
    
    /**
     * @brief 生成唯一消息ID
     * @param sender 发送者
     * @return 消息ID
     */
    QString generateMessageId(const QString& sender);
    
    /**
     * @brief 更新统计信息
     * @param sent 是否为发送
     * @param latency 延迟时间
     */
    void updateStats(bool sent, double latency = 0.0);
    
private:
    static ThreadCommunicationHub* s_instance;  // 单例实例
    
    mutable QMutex m_mutex;                     // 互斥锁
    QMap<QString, std::shared_ptr<IMessageHandler>> m_handlers;  // 消息处理器映射
    QQueue<ThreadMessage> m_messageQueue;       // 消息队列
    MessageStats m_stats;                       // 统计信息
    QTimer* m_cleanupTimer;                     // 清理定时器
    int m_maxQueueSize;                         // 最大队列大小
    quint64 m_messageCounter;                   // 消息计数器
};

/**
 * @brief 函数式消息处理器
 * 允许使用lambda或函数指针作为消息处理器
 */
class FunctionalMessageHandler : public IMessageHandler
{
public:
    using HandlerFunction = std::function<bool(const ThreadMessage&)>;
    
    /**
     * @brief 构造函数
     * @param name 处理器名称
     * @param handler 处理函数
     */
    FunctionalMessageHandler(const QString& name, HandlerFunction handler)
        : m_name(name), m_handler(handler) {}
    
    bool handleMessage(const ThreadMessage& message) override {
        return m_handler ? m_handler(message) : false;
    }
    
    QString handlerName() const override {
        return m_name;
    }
    
private:
    QString m_name;
    HandlerFunction m_handler;
};

/**
 * @brief 便利宏定义
 */
#define REGISTER_MESSAGE_HANDLER(threadName, handler) \
    ThreadCommunicationHub::instance()->registerHandler(threadName, std::make_shared<FunctionalMessageHandler>(threadName, handler))

#define SEND_COMMAND(sender, receiver, command, data) \
    ThreadCommunicationHub::instance()->sendCommand(sender, receiver, command, data)

#define SEND_DATA(sender, receiver, data) \
    ThreadCommunicationHub::instance()->sendData(sender, receiver, data)

#define SEND_STATUS(sender, receiver, status) \
    ThreadCommunicationHub::instance()->sendStatus(sender, receiver, status)

#define SEND_ERROR(sender, receiver, error) \
    ThreadCommunicationHub::instance()->sendError(sender, receiver, error)

#define BROADCAST_MESSAGE(sender, message, type) \
    ThreadCommunicationHub::instance()->broadcastMessage(sender, message, type)

#endif // THREADCOMMUNICATION_H