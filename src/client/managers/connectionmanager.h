#ifndef CONNECTIONMANAGER_H
#define CONNECTIONMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <functional>
#include "../../common/core/config/NetworkConstants.h"

class QTimer;
class TcpClient;
class IMessageCodec;

class ConnectionManager : public QObject
{
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
    
    explicit ConnectionManager(QObject *parent = nullptr);
    ~ConnectionManager();
    
    // 连接控制
    void connectToHost(const QString &host, int port);
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
    void authenticationFailed(const QString &reason);
    void errorOccurred(const QString &error);
    
private slots:
    void onTcpConnected();
    void onTcpDisconnected();
    void onTcpAuthenticated();
    void onTcpAuthenticationFailed(const QString &reason);
    void onTcpError(const QString &error);
    void onConnectionTimeout();
    void onReconnectTimer();
    
private:
    void setConnectionState(ConnectionState state);
    void setupTcpClient();
    void cleanupConnection();
    void startAutoReconnect();
    void stopAutoReconnect();
    
    TcpClient *m_tcpClient;
    ConnectionState m_connectionState;
    QString m_currentHost;
    int m_currentPort;
    QTimer *m_connectionTimer;
    
    // 自动重连相关
    QTimer *m_reconnectTimer;
    bool m_autoReconnect;
    int m_reconnectInterval;
    int m_maxReconnectAttempts;
    int m_currentReconnectAttempts;

    // 连接超时
    int m_connectionTimeout;

    // 可选编解码器工厂（返回指针，默认由 TcpClient 接管）
    std::function<IMessageCodec*()> m_codecFactory;
    
    static const int CONNECTION_TIMEOUT = NetworkConstants::DEFAULT_CONNECTION_TIMEOUT;
    static const int DEFAULT_RECONNECT_INTERVAL = NetworkConstants::DEFAULT_RECONNECT_INTERVAL;
    static const int DEFAULT_MAX_RECONNECT_ATTEMPTS = 5;
};

#endif // CONNECTIONMANAGER_H