#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QTimer>

#include "common/error/RdError.h"
#include "common/network/Protocol.h"

class IMessageCodec;
class TcpClient;

/**
 * @brief ConnectionManager 负责管理连接、握手和认证
 *
 * 职责：
 * - 管理连接状态和自动重连
 * - 处理握手和认证逻辑
 * - 提供消息发送接口
 * - 转发消息给上层处理
 *
 * 不负责：
 * - 业务数据处理（屏幕数据、输入事件等）
 * - 这些由 ProtocolSession 处理
 */
class ConnectionManager : public QObject {
    Q_OBJECT

public:
    enum ConnectionState {
        Connecting,
        Connected,
        Authenticated,
        Reconnecting,
        Disconnecting,
        Disconnected,
        Error,
        AuthFailed       ///< 认证被拒——永久终端态，阻止自动重连
    };
    Q_ENUM(ConnectionState);

    explicit ConnectionManager(QObject* parent = nullptr);
    ~ConnectionManager();

    // 连接控制
    void connectToHost(const QString& host, int port);
    void disconnectFromHost();

    // 状态查询
    virtual bool isConnected() const;
    virtual bool isAuthenticated() const;

    /// 预设认证凭证（在 connectToHost 前调用），不触发认证流程
    void setCredentials(const QString& username, const QString& password);

    /// 更新凭据（用于认证失败后重试，跨线程安全）
    Q_INVOKABLE void updateCredentials(const QString& username, const QString& password);

    /// 认证失败后重试（重置状态 + 重新连接）
    Q_INVOKABLE void retryAuthentication();

    /// 设置客户端颜色深度（在 connectToHost 前调用，由握手携带）
    void setColorDepth(int depth);
    /// 设置 JPEG 图像质量（在 connectToHost 前调用，由握手携带）
    void setImageQuality(int quality);

    // 消息发送接口
    virtual void sendMessage(MessageType type, const IMessageCodec& message);

    // 自动重连管理
    void setAutoReconnect(bool enable);
    void setReconnectInterval(int msecs);

    // 连接超时管理
    void setConnectionTimeout(int msecs);

    /// 握手响应中获取的远程屏幕尺寸
    QSize remoteScreenSize() const { return m_remoteScreenSize; }

    /// 当前凭据（认证失败重试时会经 updateCredentials 更新；认证成功后即实际通过的凭据）
    QString username() const { return m_username; }
    QString password() const { return m_password; }

signals:
    // 状态变化通知信号（用于 UI 状态显示）
    void connectionStateChanged(ConnectionState state);

    /// 连接层错误（携带可读原因，如认证失败的具体类型）
    void errorOccurred(const RdError& error);

    // 通用消息转发信号 - 供上层业务处理
    void messageReceived(MessageType type, const QByteArray& payload);

private slots:
    void onTcpConnected();
    void onTcpDisconnected();
    void onTcpError(const RdError& error);
    void onConnectionTimeout();
    void onReconnectTimer();
    void onTcpMessageReceived(MessageType type, const QByteArray& payload);

private:
    void setConnectionState(ConnectionState state);
    void setupTcpClient();
    void cleanupConnection();
    /// @return true 表示重连定时器已武装（含已活跃），false 表示不重连
    [[nodiscard]] bool startAutoReconnect();
    void stopAutoReconnect();

    // 连接相关处理方法（握手和认证）
    void handleHandshakeResponse(const QByteArray& data);
    void handleAuthenticationResponse(const QByteArray& data);
    void handleAuthChallenge(const QByteArray& data);

    void sendHandshakeRequest();
    void sendSessionCapabilities();
    QString getClientOS();

    /// 内部：保持 host/port 并发起 TCP 连接（供 onReconnectTimer 使用，
    /// 绕过 public connectToHost 的用户意图复位）
    void startConnection(const QString& host, int port);

private:
    TcpClient* m_tcpClient;
    ConnectionState m_connectionState;
    QString m_currentHost;
    int m_currentPort;
    QTimer* m_connectionTimer;

    // 持久化连接目标（认证失败重试时使用，不被 cleanupConnection 清除）
    QString m_host;
    int m_port = 0;

    // 认证信息
    QString m_username;
    QString m_password;

    // 显示参数（由握手携带到服务端）
    int m_colorDepth = 32;
    int m_imageQuality = 85;

    // 自动重连相关
    QTimer* m_reconnectTimer;
    bool m_autoReconnect;
    int m_reconnectInterval;
    int m_maxReconnectAttempts;
    int m_currentReconnectAttempts;

    // 远程屏幕尺寸（握手时获取）
    QSize m_remoteScreenSize;

    // 连接超时
    int m_connectionTimeout;

    /// 一次性用户意图标记：disconnectFromHost 无条件置 true，
    /// onTcpDisconnected 消费后清零。connectToHost 在 abort 后二次清零。
    /// 不持久化——不会被 onTcpError/onConnectionTimeout 读取或修改。
    bool m_userInitiatedDisconnect = false;

    /// 错误处理中的重入守卫：onTcpError/onConnectionTimeout 在 abort() 前置 true，
    /// 阻止同步触发的 onTcpDisconnected 独立决策（state/cleanup/reconnect），
    /// 将决策权统一保留给外层错误/超时处理函数。
    bool m_handlingError = false;

};

