#include <QtTest>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtNetwork/QSslSocket>
#include <QSignalSpy>

#include "client/network/ConnectionManager.h"
#include "client/network/ServerTrustStore.h"
#include "common/config/SettingsManager.h"
#include "server/service/TcpServer.h"

namespace {
/// 预置空 JSON，使 load() 走解析路径而非迁移路径（迁移会擦除宿主机真实遗留设置）
void seedEmptyConfig(const QString& path) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("{}");
}
bool sawState(const QSignalSpy& spy, ConnectionManager::ConnectionState want) {
    for (const QVariantList& call : spy) {
        if (call.at(0).value<ConnectionManager::ConnectionState>() == want) {
            return true;
        }
    }
    return false;
}
} // namespace

// 真实 TLS：服务端仅完成握手（不处理 RDCP），验证客户端 TOFU 记录真实证书指纹
class TofuHandshakeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void firstUseRecordsRealFingerprintThenTrusted();
private:
    QList<QSslSocket*> m_serverSockets;   // 保活已握手的服务端 socket
};

void TofuHandshakeTest::initTestCase() {
    qRegisterMetaType<ConnectionManager::ConnectionState>("ConnectionManager::ConnectionState");
}

void TofuHandshakeTest::firstUseRecordsRealFingerprintThenTrusted() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // ── 服务端：持久证书 + 监听 ──
    seedEmptyConfig(dir.filePath("server.json"));
    SettingsManager serverCfg(dir.filePath("server.json"));
    serverCfg.load();
    TcpServer server(nullptr, &serverCfg);
    connect(&server, &TcpServer::newClientConnection, this, [&](qintptr desc) {
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
    const QByteArray serverFp = server.sslCertificate().digest(QCryptographicHash::Sha256);
    QVERIFY(!serverFp.isEmpty());

    // ── 客户端：独立信任库 ──
    seedEmptyConfig(dir.filePath("client.json"));
    SettingsManager clientCfg(dir.filePath("client.json"));
    clientCfg.load();
    const QString endpoint = QStringLiteral("127.0.0.1:%1").arg(port);

    // 首连：TOFU 记录真实指纹
    {
        ConnectionManager cm(nullptr, &clientCfg);
        QSignalSpy identity(&cm, &ConnectionManager::serverIdentityChanged);
        QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
        cm.connectToHost("127.0.0.1", port);
        QTRY_VERIFY_WITH_TIMEOUT(sawState(states, ConnectionManager::Connected), 5000);
        QCOMPARE(identity.count(), 0);

        ServerTrustStore store(clientCfg);
        QCOMPARE(store.storedFingerprint(endpoint), QString::fromLatin1(serverFp.toHex()));
        QCOMPARE(store.storedFingerprint(endpoint).size(), 64);   // SHA-256 hex 长度
        cm.disconnectFromHost();
        QTest::qWait(200);
    }

    // 重连：命中 Trusted，指纹稳定，无误报
    {
        ConnectionManager cm(nullptr, &clientCfg);
        QSignalSpy identity(&cm, &ConnectionManager::serverIdentityChanged);
        QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
        cm.connectToHost("127.0.0.1", port);
        QTRY_VERIFY_WITH_TIMEOUT(sawState(states, ConnectionManager::Connected), 5000);
        QCOMPARE(identity.count(), 0);

        ServerTrustStore store(clientCfg);
        QCOMPARE(store.storedFingerprint(endpoint), QString::fromLatin1(serverFp.toHex()));
        QCOMPARE(clientCfg.trustedHosts().size(), 1);   // 未新增条目
        cm.disconnectFromHost();
        QTest::qWait(200);
    }

    server.stopServer(true);
    qDeleteAll(m_serverSockets);
    m_serverSockets.clear();
}

QTEST_MAIN(TofuHandshakeTest)
#include "test_tofu_handshake.moc"
