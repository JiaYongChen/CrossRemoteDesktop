#ifndef CONNECTIONMANAGER_H
#define CONNECTIONMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QMutex>
#include <QtGui/QImage>
#include <functional>
#include "../../common/core/config/NetworkConstants.h"
#include "../../common/core/network/Protocol.h"

class QTimer;
class TcpClient;
class IMessageCodec;

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
 * - 这些由 SessionManager 处理
 */
class ConnectionManager : public QObject {
    Q_OBJECT

public:
    enum ConnectionState {
        Connecting,
        Connected,
        Authenticating,
        Authenticated,
        Reconnecting,
        Disconnecting,
        Disconnected,
        Error
    };
    Q_ENUM(ConnectionState)

        explicit ConnectionManager(QObject* parent = nullptr);
    ~ConnectionManager();

    // 连接控制
    void connectToHost(const QString& host, int port);
    void disconnectFromHost();
    void abort();

    // 状态查询
    ConnectionState connectionState() const;
    bool isConnected() const;
    bool isAuthenticated() const;

    // 连接信息
    QString currentHost() const;
    int currentPort() const;
    QString sessionId() const;

    // 网络客户端访问
    TcpClient* tcpClient() const;

    // 认证接口
    void authenticate(const QString& username, const QString& password);

    // 消息发送接口
    void sendMessage(MessageType type, const IMessageCodec& message);

    // 自动重连管理
    void setAutoReconnect(bool enable);
    bool autoReconnect() const;
    void setReconnectInterval(int msecs);
    int reconnectInterval() const;
    void setMaxReconnectAttempts(int attempts);
    int maxReconnectAttempts() const;
    int currentReconnectAttempts() const;

    // 连接超时管理
    void setConnectionTimeout(int msecs);
    int connectionTimeout() const;

signals:
    void connectionStateChanged(ConnectionState state);
    void connected();
    void disconnected();
    void authenticated();
    void authenticationFailed(const QString& reason);
    void errorOccurred(const QString& error);

    // 通用消息转发信号 - 供上层业务处理
    void messageReceived(MessageType type, const QByteArray& payload);

private slots:
    void onTcpConnected();
    void onTcpDisconnected();
    void onTcpError(const QString& error);
    void onConnectionTimeout();
    void onReconnectTimer();
    void onTcpMessageReceived(MessageType type, const QByteArray& payload);

private:
    void setConnectionState(ConnectionState state);
    void setupTcpClient();
    void cleanupConnection();
    void startAutoReconnect();
    void stopAutoReconnect();

    // 连接相关处理方法（握手和认证）
    void handleHandshakeResponse(const QByteArray& data);
    void handleAuthenticationResponse(const QByteArray& data);
    void handleAuthChallenge(const QByteArray& data);

    void sendHandshakeRequest();
    void sendAuthenticationRequest(const QString& username, const QString& password);
    QString getClientOS();

    TcpClient* m_tcpClient;
    ConnectionState m_connectionState;
    QString m_currentHost;
    int m_currentPort;
    QTimer* m_connectionTimer;

    // 认证信息
    QString m_sessionId;
    QString m_username;
    QString m_password;

    // 自动重连相关
    QTimer* m_reconnectTimer;
    bool m_autoReconnect;
    int m_reconnectInterval;
    int m_maxReconnectAttempts;
    int m_currentReconnectAttempts;

    // 连接超时
    int m_connectionTimeout;

    static const int CONNECTION_TIMEOUT = NetworkConstants::DEFAULT_CONNECTION_TIMEOUT;
    static const int DEFAULT_RECONNECT_INTERVAL = NetworkConstants::DEFAULT_RECONNECT_INTERVAL;
    static const int DEFAULT_MAX_RECONNECT_ATTEMPTS = 5;
};

#endif // CONNECTIONMANAGER_H