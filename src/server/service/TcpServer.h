#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QtNetwork/QTcpServer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslKey>
#include "../../common/core/network/Protocol.h"
#include "../../common/core/config/NetworkConstants.h"

class QHostAddress;
class InputSimulator;

class TcpServer : public QTcpServer {
    Q_OBJECT

public:
    explicit TcpServer(QObject* parent = nullptr);
    ~TcpServer();

    // 服务器控制
    bool startServer(quint16 port = 5900, const QHostAddress& address = QHostAddress::Any);
    void stopServer();
    void stopServer(bool synchronous);
    bool isRunning() const;

    // 服务器信息
    quint16 serverPort() const;
    QHostAddress serverAddress() const;

    // TLS证书访问（供ClientHandlerWorker使用）
    QSslCertificate sslCertificate() const { return m_sslCertificate; }
    QSslKey sslPrivateKey() const { return m_sslPrivateKey; }

signals:
    void serverStopped();
    void newClientConnection(qintptr socketDescriptor);
    void errorOccurred(const QString& error);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    void cleanup();
    bool generateSelfSignedCertificate();

    // 服务器状态
    bool m_isRunning;
    quint16 m_serverPort;
    QHostAddress m_serverAddress;

    // TLS证书和密钥
    QSslCertificate m_sslCertificate;
    QSslKey m_sslPrivateKey;
};

#endif // TCPSERVER_H