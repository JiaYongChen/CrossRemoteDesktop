#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QtCore/QObject>
#include <QtCore/QDateTime>
#include <QtNetwork/QAbstractSocket>
#include "../common/core/network/Protocol.h"
#include "../common/core/config/NetworkConstants.h"

class QTcpSocket;
class QTimer;

/**
 * @brief TcpClient 只负责底层网络通信
 *
 * 职责：
 * - 管理 QTcpSocket 连接
 * - 管理接收缓冲区(m_receiveBuffer)
 * - 处理心跳机制
 * - 提供 sendMessage 接口发送消息
 * - 解析消息并发出 messageReceived 信号
 */
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

    // 服务器信息
    QString serverAddress() const;
    quint16 serverPort() const;

    // 消息发送 - 底层接口
    void sendMessage(MessageType type, const IMessageCodec& message);

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& error);

    // 通用消息接收信号 - 由 ConnectionManager 处理具体业务逻辑
    void messageReceived(MessageType type, const QByteArray& payload);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);

private:
    void processMessage(const MessageHeader& header, const QByteArray& payload);
    void handleHeartbeat();
    void checkHeartbeat();

    // 网络
    QTcpSocket* m_socket;
    QByteArray m_receiveBuffer;

    // 连接信息
    QString m_hostName;
    quint16 m_port;

    // 心跳相关 - 仅接收服务端心跳请求并响应
    QTimer* m_heartbeatCheckTimer;
    QDateTime m_lastHeartbeat;
};

#endif // TCPCLIENT_H