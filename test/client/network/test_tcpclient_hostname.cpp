#include <QtTest>
#include <QSignalSpy>
#include <QtNetwork/QSslError>
#include <QtNetwork/QSslSocket>

#include "client/network/TcpClient.h"
#include "server/service/TcpServer.h"

/// Qt 客户端模式握手无条件做主机名校验（Qt 6.9 无关闭开关），服务端证书必须含匹配的 SAN：
/// 按 IP 连接匹配 IP SAN、按主机名连接匹配 DNS SAN——两者都不得再产生 HostNameMismatch
class TcpClientHostnameTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void noHostnameMismatchDuringHandshake();
private:
    QList<QSslSocket*> m_serverSockets;   // 保活已握手的服务端 socket
};

void TcpClientHostnameTest::initTestCase() {
    qRegisterMetaType<QList<QSslError>>("QList<QSslError>");
}

void TcpClientHostnameTest::noHostnameMismatchDuringHandshake() {
    // 服务端：TcpServer 生成的自签名证书（CN + 覆盖常见连接目标的 SAN）
    TcpServer server(nullptr, nullptr);
    connect(&server, &TcpServer::newClientConnection, this, [this, &server](qintptr desc) {
        auto* sock = new QSslSocket(this);
        m_serverSockets.append(sock);
        sock->setSocketDescriptor(desc);
        sock->setLocalCertificate(server.sslCertificate());
        sock->setPrivateKey(server.sslPrivateKey());
        sock->setPeerVerifyMode(QSslSocket::VerifyNone);
        connect(sock, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
                sock, [sock](const QList<QSslError>&) { sock->ignoreSslErrors(); });
        sock->startServerEncryption();
    });
    QVERIFY(server.startServer(0));
    const quint16 port = server.serverPort();

    // 分别经 IP（匹配 IP SAN）与主机名（匹配 DNS SAN）连接验证
    const QStringList hosts = { QStringLiteral("127.0.0.1"), QStringLiteral("localhost") };
    for (const QString& host : hosts) {
        TcpClient client;
        auto* clientSock = client.findChild<QSslSocket*>();
        QVERIFY(clientSock);
        QSignalSpy sslSpy(clientSock, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors));

        client.connectToHost(host, port);
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);

        // 握手全程不得出现 HostNameMismatch
        for (const QVariantList& emission : sslSpy) {
            const auto errors = emission.at(0).value<QList<QSslError>>();
            for (const QSslError& err : errors) {
                QVERIFY2(err.error() != QSslError::HostNameMismatch,
                         qPrintable(QStringLiteral("TLS handshake to %1 must not produce hostname mismatch")
                                        .arg(host)));
            }
        }

        client.disconnectFromHost();
        QTest::qWait(100);
    }

    server.stopServer(true);
    qDeleteAll(m_serverSockets);
    m_serverSockets.clear();
}

QTEST_MAIN(TcpClientHostnameTest)
#include "test_tcpclient_hostname.moc"
