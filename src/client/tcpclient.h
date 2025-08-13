#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QtCore/QObject>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include "../common/core/protocol.h"
#include "../core/networkconstants.h"

class TcpClient : public QObject
{
    Q_OBJECT
    
public:
    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient();
    
    // 连接控制
    void connectToHost(const QString &hostName, quint16 port);
    void disconnectFromHost();
    void abort();
    
    // 连接状态
    bool isConnected() const;
    bool isAuthenticated() const;
    
    // 服务器信息
    QString serverAddress() const;
    quint16 serverPort() const;
    QString sessionId() const;
    
    // 认证
    void authenticate(const QString &username, const QString &password);
    
    // 消息发送
    void sendMessage(MessageType type, const QByteArray &data = QByteArray());
    
    // 配置
    void setConnectionTimeout(int msecs);
    int connectionTimeout() const;
    
signals:
    void connected();
    void disconnected();
    void authenticated();
    void authenticationFailed(const QString &reason);
    void messageReceived(MessageType type, const QByteArray &data);

    void errorOccurred(const QString &error);
    void statusUpdated(const QString &status);
    void screenDataReceived(const QPixmap &frame);
    
public slots:
    
    // 输入事件发送
    void sendMouseEvent(int x, int y, int buttons, int eventType);
    void sendKeyboardEvent(int key, int modifiers, bool pressed, const QString &text);
    void sendWheelEvent(int x, int y, int delta, int orientation);
    
private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void onConnectionTimeout();
    void sendHeartbeat();
    
private:
    void processMessage(const MessageHeader &header, const QByteArray &payload);
    void handleHandshakeResponse(const QByteArray &data);
    void handleAuthenticationResponse(const QByteArray &data);
    void handleHeartbeat();
    void handleErrorMessage(const QByteArray &data);
    void handleStatusUpdate(const QByteArray &data);
    void handleDisconnectRequest();
    void handleScreenData(const QByteArray &data);
    
    void sendHandshakeRequest();
    void sendAuthenticationRequest(const QString &username, const QString &password);
    void sendDisconnectRequest();
    
    void resetConnection();
    
    QString hashPassword(const QString &password);
    QString getClientName();
    QString getClientOS();
    
    // 网络
    QTcpSocket *m_socket;
    QByteArray m_receiveBuffer;
    
    // 连接信息
    QString m_hostName;
    quint16 m_port;
    QString m_sessionId;

    // 认证信息
    QString m_username;
    QString m_password;
    
    // 定时器
    QTimer *m_connectionTimer;
    QTimer *m_heartbeatTimer;
    
    // 连接超时
    int m_connectionTimeout;
    
    // 差异压缩相关
    QByteArray m_previousFrameData;
    mutable QMutex m_frameDataMutex;
    
    // 线程安全
    QMutex m_mutex;
    
    // 常量
    static const int DEFAULT_RECONNECT_INTERVAL = NetworkConstants::DEFAULT_RECONNECT_INTERVAL;
    static const int DEFAULT_CONNECTION_TIMEOUT = NetworkConstants::DEFAULT_CONNECTION_TIMEOUT;
    static const int HEARTBEAT_INTERVAL = NetworkConstants::HEARTBEAT_INTERVAL;
};

#endif // TCPCLIENT_H