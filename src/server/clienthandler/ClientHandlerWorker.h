#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtNetwork/QAbstractSocket>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslKey>
#include <QtNetwork/QSslSocket>

#include <atomic>

#include "common/error/RdError.h"
#include "common/network/Protocol.h"
#include "common/threading/Worker.h"

class AuthHandler;
class InputSimulator;
class IMessageCodec;
class ScreenCaptureWorker;
struct ProcessedData;
template<typename T>
class ThreadSafeQueue;

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
    explicit ClientHandlerWorker(qintptr socketDescriptor,
                                ThreadSafeQueue<ProcessedData>* processedQueue,
                                const QString& serverUsername,
                                const QString& serverPassword,
                                const QSslCertificate& certificate = QSslCertificate(),
                                const QSslKey& privateKey = QSslKey(),
                                QObject* parent = nullptr);

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
     * @brief 设置认证期望的用户名
     * @param username 期望的用户名
     */
    Q_INVOKABLE void setExpectedUsername(const QString& username);

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

    /**
     * @brief 设置 ScreenCaptureWorker 并连接光标更新信号
     * @param worker ScreenCaptureWorker 指针
     */
    void setScreenCaptureWorker(ScreenCaptureWorker* worker);

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
     * @brief 发生错误信号
     * @param error 错误信息
     */
    void errorOccurred(const RdError& error);

    /**
     * @brief 客户端 ENCODE_PREFS 消息携带的图像质量参数
     * @param imageQuality JPEG 编码质量 (1-100)
     */
    void qualitySettingsReceived(int imageQuality);

    /**
     * @brief 客户端 ENCODE_PREFS 消息携带的色深参数
     * @param colorDepth 色深值 (16/24/32)
     */
    void colorDepthReceived(int colorDepth);

    /**
     * @brief 接收到客户端剪贴板数据（已认证）
     * @param message 剪贴板消息
     */
    void clipboardDataReceived(const ClipboardMessage& message);

    /**
     * @brief 客户端请求剪贴板文件数据（粘贴方 → 复制方）
     * @param fileIndex 文件在 FILE_LIST 中的索引
     */
    void fileContentRequestReceived(quint32 fileIndex);

    /**
     * @brief 客户端发送剪贴板文件数据块（复制方 → 粘贴方）
     * @param fileIndex 文件索引
     * @param data 数据块
     * @param flags 标志（bit 0 = lastChunk）
     */
    void clipboardFileChunkReceived(quint32 fileIndex, const QByteArray& data, quint8 flags);

    /**
     * @brief 客户端发起大文件分块传输（粘贴方 → 复制方）
     * @param fileIndex 文件索引
     */
    void fileTransferInitReceived(quint32 fileIndex);

    /**
     * @brief 客户端发送大文件数据块（复制方 → 粘贴方）
     * @param fileIndex 文件索引
     * @param seq 块序号
     * @param data 数据块
     */
    void fileTransferChunkReceived(quint32 fileIndex, quint32 seq, const QByteArray& data);

    /**
     * @brief 客户端确认大文件数据块（粘贴方 → 复制方）
     * @param fileIndex 文件索引
     * @param ackSeq 已确认的最大 SEQ
     */
    void fileChunkAckReceived(quint32 fileIndex, quint32 ackSeq);

    /**
     * @brief 客户端取消文件传输（双向）
     * @param fileIndex 文件索引
     */
    void fileTransferCancelled(quint32 fileIndex);

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
     * @brief 处理版本交换请求
     * @param data 请求数据
     */
    void handleVersionExchange(const QByteArray& data);

    /**
     * @brief 同步配置认证状态（按是否设置密码分流）
     *
     * 无密码 → AuthHandler 标记直通；有密码 → 生成每连接 salt + PBKDF2 派生期望摘要。
     * 由 handleVersionExchange 在本线程同步调用，随后下发版本交换响应。
     */
    void setupAuthentication();

    /**
     * @brief 处理编码偏好（编码参数）单向通知
     *
     * 仅接受已认证客户端；解码成功后复用 qualitySettingsReceived /
     * colorDepthReceived 信号通知编码器。
     * @param data 消息数据
     */
    void handleEncodePrefs(const QByteArray& data);

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
     * @brief 处理剪贴板文件请求
     * @param data 请求数据
     */
    void handleClipboardFileRequest(const QByteArray& data);

    /**
     * @brief 处理剪贴板文件数据块
     * @param data 数据块数据
     */
    void handleClipboardFileChunk(const QByteArray& data);

    /**
     * @brief 处理大文件传输发起请求
     * @param data 请求数据
     */
    void handleFileTransferInit(const QByteArray& data);

    /**
     * @brief 处理大文件传输数据块
     * @param data 数据块数据
     */
    void handleFileTransferChunk(const QByteArray& data);

    /**
     * @brief 处理大文件传输确认
     * @param data 确认数据
     */
    void handleFileTransferAck(const QByteArray& data);

    /**
     * @brief 处理大文件传输取消
     * @param data 取消数据
     */
    void handleFileTransferCancel(const QByteArray& data);

    /**
     * @brief 发送版本交换响应（携带认证参数）
     *
     * 由 handleVersionExchange 在 setupAuthentication 同步配置认证后调用；
     * 无密码模式同时直通认证。
     */
    void deliverVersionExchangeResponse();

    /**
     * @brief 认证成功公共序列：置认证态、发会话 ID 与成功响应、发射 authenticated()
     * @param mode 认证模式描述，仅用于日志（如「密码模式」「无密码模式」）
     */
    void acceptAuthentication(const QString& mode);

    /**
     * @brief 发送认证响应
     * @param result 认证结果
     * @param sessionId 会话ID
     */
    void sendAuthenticationResponse(AuthResult result, const QString& sessionId = QString());

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
    
private:
    // 网络相关
    qintptr m_socketDescriptor;           ///< 套接字描述符
    QSslSocket* m_socket;                 ///< TLS套接字

    // TLS证书和密钥
    QSslCertificate m_sslCertificate;     ///< TLS证书
    QSslKey m_sslPrivateKey;              ///< TLS私钥
    QByteArray m_receiveBuffer;           ///< 接收缓冲区

    // 客户端信息（线程安全访问需要互斥锁）
    mutable QMutex m_clientInfoMutex;     ///< 客户端信息互斥锁
    QString m_clientAddress;              ///< 客户端地址
    quint16 m_clientPort;                 ///< 客户端端口
    QString m_clientId;                   ///< 客户端ID
    bool m_isAuthenticated;               ///< 是否已认证
    bool m_versionExchangeProcessed = false;    ///< 版本交换是否已处理（幂等守卫：阻止重复版本交换重设 AuthHandler）

    // 认证（委托给 AuthHandler）
    AuthHandler* m_authHandler;           ///< 认证处理器
    QString m_serverUsername;             ///< 服务端认证用户名（无密码模式可为空）
    QString m_serverPassword;             ///< 服务端认证密码（空 = 无密码模式）

    // 时间和心跳
    QDateTime m_connectionTime;           ///< 连接时间
    QDateTime m_lastHeartbeat;            ///< 最后心跳时间
    QTimer* m_heartbeatSendTimer;         ///< 心跳发送定时器
    QTimer* m_heartbeatCheckTimer;        ///< 心跳检查定时器

    // 光标位置发送
    ScreenCaptureWorker* m_screenCaptureWorker{ nullptr };  ///< 屏幕捕获工作线程（非拥有指针）

    // 连接状态（线程安全，用于跨线程查询替代直接访问 QSslSocket::state()）
    std::atomic<bool> m_isConnectedAtomic{ false };

    // 断开连接标志（避免重复发送disconnected信号）
    std::atomic<bool> m_disconnectSignalSent{ false };

    // 统计信息（线程安全访问需要互斥锁）
    mutable QMutex m_statsMutex;          ///< 统计信息互斥锁
    quint64 m_bytesReceived;              ///< 接收字节数
    quint64 m_bytesSent;                  ///< 发送字节数

    // 输入模拟器
    InputSimulator* m_inputSimulator;     ///< 输入模拟器

    // 屏幕数据发送相关
    ThreadSafeQueue<ProcessedData>* m_processedQueue = nullptr;  ///< 处理队列（数据来源）

    // Guard flag to prevent event queue accumulation:
    // processTask posts sendScreenDataFromQueue via QueuedConnection on each tick;
    // without this flag, pending invocations pile up if the event loop is slow.
    std::atomic<bool> m_sendScreenDataPending{ false };
};

