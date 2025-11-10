#ifndef CLIENTHANDLERWORKER_H
#define CLIENTHANDLERWORKER_H

#include "../../common/core/threading/Worker.h"
#include "../../common/core/network/Protocol.h"
#include <QtCore/QObject>
#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include <QtCore/QTimer>
#include <QtNetwork/QAbstractSocket>
#include <QtNetwork/QTcpSocket>

class InputSimulator;
class IMessageCodec;
class QueueManager;

/**
 * @brief 客户端处理工作线程类
 *
 * 继承Worker基类，在独立线程中处理客户端连接和通信。
 * 支持认证、心跳检测、输入事件处理等功能。
 * 设计为单连接模式，每个实例只处理一个客户端连接。
 * 认证成功后自动从处理队列拉取并发送屏幕数据。
 */
class ClientHandlerWorker : public Worker {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param socketDescriptor 套接字描述符
     * @param parent 父对象
     */
    explicit ClientHandlerWorker(qintptr socketDescriptor, QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~ClientHandlerWorker() override;

    /**
     * @brief 禁用拷贝构造和赋值操作
     */
    ClientHandlerWorker(const ClientHandlerWorker&) = delete;
    ClientHandlerWorker& operator=(const ClientHandlerWorker&) = delete;

    // 连接信息获取方法（线程安全）
    QString clientAddress() const;
    quint16 clientPort() const;
    QString clientId() const;
    bool isConnected() const;
    bool isAuthenticated() const;

    // 统计信息获取方法（线程安全）
    quint64 bytesReceived() const;
    quint64 bytesSent() const;
    QDateTime connectionTime() const;

    /**
     * @brief 设置认证参数
     * @param salt 密码盐值
     * @param digest 密码摘要
     */
    Q_INVOKABLE void setExpectedPasswordDigest(const QByteArray& salt, const QByteArray& digest);

    /**
     * @brief 设置PBKDF2参数
     * @param iterations 迭代次数
     * @param keyLength 密钥长度
     */
    Q_INVOKABLE void setPbkdf2Params(quint32 iterations, quint32 keyLength);

    /**
     * @brief 发送消息到客户端
     * @param type 消息类型
     * @param message 消息数据（实现IMessageCodec接口）
     */
    Q_INVOKABLE void sendMessage(MessageType type, const IMessageCodec& message);

    /**
     * @brief 发送已编码的消息数据到客户端（用于非阻塞发送）
     * @param messageData 已编码的消息数据（包含消息头和负载）
     */
    Q_INVOKABLE void sendEncodedMessage(const QByteArray& messageData);

    /**
     * @brief 断开客户端连接
     */
    Q_INVOKABLE void disconnectClient();

    /**
     * @brief 强制断开连接
     */
    Q_INVOKABLE void forceDisconnect();

signals:
    /**
     * @brief 客户端断开连接信号
     */
    void disconnected();

    /**
     * @brief 客户端认证成功信号
     */
    void authenticated();

    /**
     * @brief 接收到消息信号
     * @param type 消息类型
     * @param data 消息数据
     */
    void messageReceived(MessageType type, const QByteArray& data);

    /**
     * @brief 发生错误信号
     * @param error 错误信息
     */
    void errorOccurred(const QString& error);

    /**
     * @brief 接收到剪贴板文本信号（更新服务器端剪贴板）
     * @param text 文本内容
     */
    void clipboardTextReceived(const QString& text);

    /**
     * @brief 接收到剪贴板图片信号（更新服务器端剪贴板）
     * @param imageData PNG 格式的图片数据
     */
    void clipboardImageReceived(const QByteArray& imageData);

    /**
     * @brief 广播剪贴板文本到其他客户端
     * @param text 文本内容
     */
    void broadcastClipboardText(const QString& text);

    /**
     * @brief 广播剪贴板图片到其他客户端
     * @param imageData PNG 格式的图片数据
     * @param width 图片宽度
     * @param height 图片高度
     */
    void broadcastClipboardImage(const QByteArray& imageData, quint32 width, quint32 height);

protected:
    /**
     * @brief 初始化工作线程
     * @return 是否初始化成功
     */
    bool initialize() override;

    /**
     * @brief 清理工作线程资源
     */
    void cleanup() override;

    /**
     * @brief 处理任务（主要处理网络事件）
     */
    void processTask() override;

private slots:
    /**
     * @brief 处理套接字可读事件
     */
    void onReadyRead();

    /**
     * @brief 处理套接字断开事件
     */
    void onDisconnected();

    /**
     * @brief 处理套接字错误事件
     * @param error 套接字错误
     */
    void onError(QAbstractSocket::SocketError error);

    /**
     * @brief 检查心跳超时
     */
    void checkHeartbeat();

    /**
     * @brief 发送心跳包
     */
    void sendHeartbeat();

private:
    /**
     * @brief 处理接收到的消息
     * @param header 消息头
     * @param payload 消息载荷
     */
    void processMessage(const MessageHeader& header, const QByteArray& payload);

    /**
     * @brief 处理握手请求
     * @param data 请求数据
     */
    void handleHandshakeRequest(const QByteArray& data);

    /**
     * @brief 处理认证请求
     * @param data 认证数据
     */
    void handleAuthenticationRequest(const QByteArray& data);

    /**
     * @brief 处理心跳包
     */
    void handleHeartbeat();

    /**
     * @brief 处理鼠标事件
     * @param data 鼠标事件数据
     */
    void handleMouseEvent(const QByteArray& data);

    /**
     * @brief 处理键盘事件
     * @param data 键盘事件数据
     */
    void handleKeyboardEvent(const QByteArray& data);

    /**
     * @brief 处理剪贴板消息
     * @param data 剪贴板数据
     */
    void handleClipboardData(const QByteArray& data);

    /**
     * @brief 发送握手响应
     */
    void sendHandshakeResponse();

    /**
     * @brief 发送认证响应
     * @param result 认证结果
     * @param sessionId 会话ID
     */
    void sendAuthenticationResponse(AuthResult result, const QString& sessionId = QString());

    /**
     * @brief 发送认证挑战
     */
    void sendAuthChallenge();

    /**
     * @brief 生成会话ID
     * @return 会话ID字符串
     */
    QString generateSessionId() const;

    /**
     * @brief 从处理队列发送屏幕数据
     * 认证成功后在processTask中异步调用，自动拉取并发送屏幕数据
     */
    Q_INVOKABLE void sendScreenDataFromQueue();
    
    /**
     * @brief 发送光标类型到客户端
     */
    Q_INVOKABLE void sendCursorType();

private:
    // 网络相关
    qintptr m_socketDescriptor;           ///< 套接字描述符
    QTcpSocket* m_socket;                 ///< TCP套接字
    QByteArray m_receiveBuffer;           ///< 接收缓冲区

    // 客户端信息（线程安全访问需要互斥锁）
    mutable QMutex m_clientInfoMutex;     ///< 客户端信息互斥锁
    QString m_clientAddress;              ///< 客户端地址
    quint16 m_clientPort;                 ///< 客户端端口
    QString m_clientId;                   ///< 客户端ID
    bool m_isAuthenticated;               ///< 是否已认证

    // 认证相关
    QByteArray m_expectedSalt;            ///< 期望的密码盐值
    QByteArray m_expectedDigest;          ///< 期望的密码摘要
    quint32 m_pbkdf2Iterations{ 100000 }; ///< PBKDF2迭代次数
    quint32 m_pbkdf2KeyLength{ 32 };      ///< PBKDF2密钥长度
    int m_failedAuthCount;                ///< 认证失败次数

    // 时间和心跳
    QDateTime m_connectionTime;           ///< 连接时间
    QDateTime m_lastHeartbeat;            ///< 最后心跳时间
    QTimer* m_heartbeatSendTimer;         ///< 心跳发送定时器
    QTimer* m_heartbeatCheckTimer;        ///< 心跳检查定时器
    
    // 光标位置发送
    QTimer* m_cursorUpdateTimer;          ///< 光标位置更新定时器

    // 断开连接标志（避免重复发送disconnected信号）
    std::atomic<bool> m_disconnectSignalSent{ false };

    // 统计信息（线程安全访问需要互斥锁）
    mutable QMutex m_statsMutex;          ///< 统计信息互斥锁
    quint64 m_bytesReceived;              ///< 接收字节数
    quint64 m_bytesSent;                  ///< 发送字节数

    // 输入模拟器
    InputSimulator* m_inputSimulator;     ///< 输入模拟器

    // 屏幕数据发送相关
    QueueManager* m_queueManager;         ///< 队列管理器
};

#endif // CLIENTHANDLERWORKER_H