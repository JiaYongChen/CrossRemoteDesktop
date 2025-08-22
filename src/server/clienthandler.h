#ifndef CLIENTHANDLER_H
#define CLIENTHANDLER_H

#include <QtCore/QObject>
#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include <QtNetwork/QAbstractSocket>
#include "../common/core/protocol.h"

class QTcpSocket;
class QTimer;

class InputSimulator;

// 客户端处理器类
class ClientHandler : public QObject
{
    Q_OBJECT
    
public:
    explicit ClientHandler(qintptr socketDescriptor, QObject *parent = nullptr);
    ~ClientHandler();
    
    // 连接信息
    QString clientAddress() const;
    quint16 clientPort() const;
    QString clientId() const;
    bool isConnected() const;
    bool isAuthenticated() const;
    
    // 消息发送
    void sendMessage(MessageType type, const IMessageCodec &message);
    
    // 认证配置（阶段C：摘要策略 PBKDF2-SHA256）
    void setExpectedPasswordDigest(const QByteArray &salt, const QByteArray &digest);
    void setPbkdf2Params(quint32 iterations, quint32 keyLength) { m_pbkdf2Iterations = iterations; m_pbkdf2KeyLength = keyLength; }

    // 连接控制
    void disconnectClient();
    void forceDisconnect();
    
    // 统计信息
    quint64 bytesReceived() const;
    quint64 bytesSent() const;
    QDateTime connectionTime() const;
    
signals:
    void connected();
    void disconnected();
    void authenticated();
    void messageReceived(MessageType type, const QByteArray &data);
    void errorOccurred(const QString &error);
    
private slots:
    void onReadyRead();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    void sendHeartbeat();
    void checkHeartbeat();
    
private:
    void processMessage(const MessageHeader &header, const QByteArray &payload);
    void handleHandshakeRequest(const QByteArray &data);
    void handleAuthenticationRequest(const QByteArray &data);
    void handleHeartbeat();
    void handleMouseEvent(const QByteArray &data);
    void handleKeyboardEvent(const QByteArray &data);
    void handleDisconnectRequest();
    
    void sendHandshakeResponse();
    void sendAuthenticationResponse(AuthResult result, const QString &sessionId = QString());
    QString generateSessionId() const;
    
    QTcpSocket *m_socket;
    QString m_clientAddress;
    quint16 m_clientPort;
    QString m_clientId;
    bool m_isAuthenticated;
    QByteArray m_expectedSalt;   // 阶段C：PBKDF2盐
    QByteArray m_expectedDigest; // 阶段C：PBKDF2摘要
    quint32 m_pbkdf2Iterations{100000};
    quint32 m_pbkdf2KeyLength{32};
    int m_failedAuthCount;
    QDateTime m_connectionTime;
    QDateTime m_lastHeartbeat;
    QTimer *m_heartbeatTimer;
    QTimer *m_heartbeatCheckTimer;
    quint64 m_bytesReceived;
    quint64 m_bytesSent;
    InputSimulator *m_inputSimulator;
    QByteArray m_receiveBuffer;
    int m_parseFailCount{0}; // 连续解析失败计数器，用于重同步
};

#endif // CLIENTHANDLER_H