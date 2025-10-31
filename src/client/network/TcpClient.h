#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QtCore/QObject>
#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include <QtGui/QImage>
#include <QtNetwork/QAbstractSocket>
#include "../common/core/network/Protocol.h"
#include "../common/core/config/NetworkConstants.h"

class QTcpSocket;
class QTimer;

class TcpClient : public QObject {
    Q_OBJECT

public:
    explicit TcpClient(QObject* parent = nullptr);
    ~TcpClient();

    // 连接控制
    void connectToHost(const QString& hostName, quint16 port);
    void disconnectFromHost();
    void abort();

    // 连接状态
    bool isConnected() const;
    bool isAuthenticated() const;

    // 服务器信息
    QString serverAddress() const;
    quint16 serverPort() const;
    QString sessionId() const;

    // 错误统计信息
    struct ErrorStatistics {
        quint32 decodeFailures = 0;          // 解码失败次数
        quint32 imageLoadFailures = 0;       // 图像加载失败次数
        quint32 networkErrors = 0;           // 网络错误次数
        quint32 dataCorruptions = 0;         // 数据损坏次数
        quint32 totalFramesReceived = 0;     // 总接收帧数
        QDateTime lastErrorTime;             // 最后错误时间
        QString lastErrorMessage;            // 最后错误信息
    };

    ErrorStatistics getErrorStatistics() const;

    // 认证
    void authenticate(const QString& username, const QString& password);

    // 消息发送
    void sendMessage(MessageType type, const IMessageCodec& message);

signals:
    void connected();
    void disconnected();
    void authenticated();
    void authenticationFailed(const QString& reason);

    void errorOccurred(const QString& error);
    void statusUpdated(const QString& status);
    void screenDataReceived(const QImage& frame);

public slots:

    // 输入事件发送
    void sendMouseEvent(int x, int y, int buttons, int eventType);
    void sendKeyboardEvent(int key, int modifiers, bool pressed, const QString& text);
    void sendWheelEvent(int x, int y, int delta, int orientation);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void checkHeartbeat();

private:
    void processMessage(const MessageHeader& header, const QByteArray& payload);
    void handleHandshakeResponse(const QByteArray& data);
    void handleAuthenticationResponse(const QByteArray& data);
    void handleHeartbeat();
    void handleErrorMessage(const QByteArray& data);
    void handleStatusUpdate(const QByteArray& data);
    void handleScreenData(const QByteArray& data);

    void sendHandshakeRequest();
    void sendAuthenticationRequest(const QString& username, const QString& password);

    void resetConnection();

    QString hashPassword(const QString& password);
    QString getClientName();
    QString getClientOS();

    // 网络
    QTcpSocket* m_socket;
    QByteArray m_receiveBuffer;

    // 连接信息
    QString m_hostName;
    quint16 m_port;
    QString m_sessionId;

    // 认证信息
    QString m_username;
    QString m_password;

    // 心跳相关 - 仅接收服务端心跳请求并响应
    QTimer* m_heartbeatCheckTimer;  // 用于检测服务端心跳超时

    // 帧数据缓存和线程安全
    QByteArray m_previousFrameData;
    QDateTime m_lastHeartbeat;
    mutable QMutex* m_frameDataMutex;

    // 错误统计相关成员变量
    ErrorStatistics m_errorStats;
    mutable QMutex* m_errorStatsMutex;

    // 错误统计相关辅助方法
    void recordDecodeFailure(const QString& details);
    void recordImageLoadFailure(const QString& details);
    void recordNetworkError(const QString& details);
    void recordDataCorruption(const QString& details);
};

#endif // TCPCLIENT_H