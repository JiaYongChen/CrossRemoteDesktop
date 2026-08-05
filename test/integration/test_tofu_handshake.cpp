#include <QtTest>
#include <QtCore/QCryptographicHash>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QTcpServer>
#include <QSignalSpy>

#include "client/network/ConnectionManager.h"
#include "client/network/ServerTrustStore.h"
#include "common/config/SettingsManager.h"
#include "common/error/RdError.h"
#include "common/network/Protocol.h"
#include "server/service/TcpServer.h"

namespace {
/// 预置空 JSON，使 load() 走解析路径而非迁移路径（迁移会擦除宿主机真实遗留设置）
void seedEmptyConfig(const QString& path) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("{}");
}
} // namespace

// 真实 TLS：服务端仅完成握手（不处理 RDCP 业务），验证客户端
// PKI 证书信任（VerifyNone 首连 / 证书记录后 VerifyPeer / 证书变更拒绝）
// 与协议事件信号（版本不匹配 / 认证被拒）
class TofuHandshakeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void firstUseVerifyNoneProceedsThenRecordedTrustVerifies();
    void changedCertificateRejected();
    void versionMismatchResponseEmitsSignal();
    void authenticationRejectedEmitsSignal();
private:
    QList<QSslSocket*> m_serverSockets;   // 保活已握手的服务端 socket

    /// 服务端脚手架：TLS 服务器端握手（VerifyNone + 注入持久证书）。
    /// replyFrame 非空时，TLS 完成后延迟 200ms 回送该帧（模拟服务端协议响应）
    void startTlsServer(TcpServer& server, quint16 port, const QByteArray& replyFrame = {});
};

void TofuHandshakeTest::initTestCase() {
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    qRegisterMetaType<RdError>("RdError");
    qRegisterMetaType<MessageType>("MessageType");
    qRegisterMetaType<AuthResult>("AuthResult");
}

void TofuHandshakeTest::startTlsServer(TcpServer& server, quint16 port, const QByteArray& replyFrame) {
    connect(&server, &TcpServer::newClientConnection, this, [this, &server, replyFrame](qintptr desc) {
        auto* sock = new QSslSocket(this);
        m_serverSockets.append(sock);
        sock->setSocketDescriptor(desc);
        sock->setLocalCertificate(server.sslCertificate());
        sock->setPrivateKey(server.sslPrivateKey());
        sock->setPeerVerifyMode(QSslSocket::VerifyNone);
        connect(sock, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
                sock, [sock](const QList<QSslError>&) { sock->ignoreSslErrors(); });
        if ( !replyFrame.isEmpty() ) {
            // 加密完成后延迟回送伪造响应（客户端 onTcpConnected 即发版本交换请求，
            // 单向帧无需请求-响应配对，延迟保证帧落在 TLS 完成后）
            connect(sock, &QSslSocket::encrypted, sock, [sock, replyFrame]() {
                QTimer::singleShot(200, sock, [sock, replyFrame]() {
                    if ( sock->isEncrypted() ) {
                        sock->write(replyFrame);
                    }
                });
            });
        }
        sock->startServerEncryption();
    });
    QVERIFY(server.startServer(port));
}

void TofuHandshakeTest::firstUseVerifyNoneProceedsThenRecordedTrustVerifies() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // ── 服务端：持久证书 + 监听 ──
    seedEmptyConfig(dir.filePath("server.json"));
    SettingsManager serverCfg(dir.filePath("server.json"));
    serverCfg.load();
    TcpServer server(nullptr, &serverCfg);
    startTlsServer(server, 0);
    const quint16 port = server.serverPort();
    const QSslCertificate serverCert = server.sslCertificate();
    QVERIFY(!serverCert.isNull());

    // ── 客户端：独立信任库 ──
    seedEmptyConfig(dir.filePath("client.json"));
    SettingsManager clientCfg(dir.filePath("client.json"));
    clientCfg.load();
    const QString endpoint = ServerTrustStore::endpointFor("127.0.0.1", port);

    // 首连：无信任记录 → VerifyNone 放行 → connected
    {
        ConnectionManager cm(nullptr, &clientCfg);
        QSignalSpy connectedSpy(&cm, &ConnectionManager::connected);
        cm.connectToHost("127.0.0.1", port);
        QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() > 0, 5000);

        // 首连自动记录：onTcpConnected 内通过 peerCertificate() 落库
        ServerTrustStore store(clientCfg);
        QVERIFY(store.storedCertificate(endpoint).has_value());
        cm.disconnectFromHost();
        QTest::qWait(200);
    }

    // 证书已自动记录 → 重连走 VerifyPeer：证书匹配 → 直接放行
    {
        ConnectionManager cm(nullptr, &clientCfg);
        QSignalSpy connectedSpy(&cm, &ConnectionManager::connected);
        cm.connectToHost("127.0.0.1", port);
        QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() > 0, 5000);
        QCOMPARE(clientCfg.value("trusted_certs").toJsonObject().size(), 1);   // 未新增条目
        cm.disconnectFromHost();
        QTest::qWait(200);
    }

    server.stopServer(true);
    qDeleteAll(m_serverSockets);
    m_serverSockets.clear();
}

void TofuHandshakeTest::changedCertificateRejected() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 探测空闲端口：信任记录以 endpoint（含端口）为键，变更检测必须同端口换证书
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();

    // ── 服务端 A：客户端信任其证书 ──
    seedEmptyConfig(dir.filePath("serverA.json"));
    SettingsManager serverACfg(dir.filePath("serverA.json"));
    serverACfg.load();
    TcpServer serverA(nullptr, &serverACfg);
    startTlsServer(serverA, port);
    const QSslCertificate certA = serverA.sslCertificate();

    seedEmptyConfig(dir.filePath("client.json"));
    SettingsManager clientCfg(dir.filePath("client.json"));
    clientCfg.load();
    const QString endpoint = ServerTrustStore::endpointFor("127.0.0.1", port);
    ServerTrustStore store(clientCfg);
    store.recordTrust(endpoint, certA);

    serverA.stopServer(true);

    // ── 服务端 B：新配置 → 新证书（MITM / 服务端重装场景）──
    seedEmptyConfig(dir.filePath("serverB.json"));
    SettingsManager serverBCfg(dir.filePath("serverB.json"));
    serverBCfg.load();
    TcpServer serverB(nullptr, &serverBCfg);
    startTlsServer(serverB, port);   // 同一端口重绑
    QVERIFY(serverB.sslCertificate().digest(QCryptographicHash::Sha256)
            != certA.digest(QCryptographicHash::Sha256));

    // 客户端仍信任旧证书连接新服务端 → VerifyPeer 失败 → TLS 错误
    ConnectionManager cm(nullptr, &clientCfg);
    QSignalSpy errors(&cm, &ConnectionManager::errorOccurred);
    cm.connectToHost("127.0.0.1", port);
    QTRY_VERIFY_WITH_TIMEOUT(errors.count() > 0, 5000);

    bool sawTlsError = false;
    for ( const QVariantList& call : errors ) {
        if ( call.at(0).value<RdError>().code == ErrorCode::NetworkTlsError ) {
            sawTlsError = true;
        }
    }
    QVERIFY(sawTlsError);   // 变更检测生效（证书验证失败为致命错误）
    QVERIFY(!cm.isConnected());

    // 拒绝不得覆盖信任：信任库仍保留旧证书
    const auto stored = store.storedCertificate(endpoint);
    QVERIFY(stored.has_value());
    QCOMPARE(QString::fromLatin1(stored->toPem()), QString::fromLatin1(certA.toPem()));

    cm.disconnectFromHost();
    QTest::qWait(200);
    serverB.stopServer(true);
    qDeleteAll(m_serverSockets);
    m_serverSockets.clear();
}

void TofuHandshakeTest::versionMismatchResponseEmitsSignal() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // ── 服务端：TLS 完成后回送伪造版本交换响应（版本不匹配）──
    seedEmptyConfig(dir.filePath("server.json"));
    SettingsManager serverCfg(dir.filePath("server.json"));
    serverCfg.load();
    TcpServer server(nullptr, &serverCfg);
    VersionExchangeResponse resp;
    resp.appVersion = QCoreApplication::applicationVersion() + QStringLiteral("-other");
    resp.serverName = QStringLiteral("UltraDesktop Server");
    resp.serverOS = QStringLiteral("Windows");
    const QByteArray frame = Protocol::createMessage(MessageType::VERSION_EXCHANGE_RESPONSE, resp);
    startTlsServer(server, 0, frame);
    const quint16 port = server.serverPort();

    seedEmptyConfig(dir.filePath("client.json"));
    SettingsManager clientCfg(dir.filePath("client.json"));
    clientCfg.load();

    ConnectionManager cm(nullptr, &clientCfg);
    QSignalSpy versionMismatched(&cm, &ConnectionManager::versionMismatched);
    QSignalSpy disconnected(&cm, &ConnectionManager::disconnected);
    cm.connectToHost("127.0.0.1", port);
    QTRY_VERIFY_WITH_TIMEOUT(versionMismatched.count() > 0, 5000);
    QCOMPARE(versionMismatched.at(0).at(0).toString(), resp.appVersion);
    QCOMPARE(versionMismatched.at(0).at(1).toString(), QCoreApplication::applicationVersion());
    // 版本闸门拒绝后立即断连（不得继续认证流程）
    QTRY_VERIFY_WITH_TIMEOUT(disconnected.count() > 0, 5000);

    cm.disconnectFromHost();
    QTest::qWait(200);
    server.stopServer(true);
    qDeleteAll(m_serverSockets);
    m_serverSockets.clear();
}

void TofuHandshakeTest::authenticationRejectedEmitsSignal() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // ── 服务端：TLS 完成后直通伪造认证响应（无密码模式，拒绝）──
    seedEmptyConfig(dir.filePath("server.json"));
    SettingsManager serverCfg(dir.filePath("server.json"));
    serverCfg.load();
    TcpServer server(nullptr, &serverCfg);
    AuthenticationResponse denied;
    denied.result = AuthResult::INVALID_CREDENTIALS;
    denied.sessionId = QStringLiteral("denied");
    const QByteArray frame = Protocol::createMessage(MessageType::AUTHENTICATION_RESPONSE, denied);
    startTlsServer(server, 0, frame);
    const quint16 port = server.serverPort();

    seedEmptyConfig(dir.filePath("client.json"));
    SettingsManager clientCfg(dir.filePath("client.json"));
    clientCfg.load();

    ConnectionManager cm(nullptr, &clientCfg);
    QSignalSpy authFailed(&cm, &ConnectionManager::authenticationFailed);
    QSignalSpy disconnected(&cm, &ConnectionManager::disconnected);
    cm.connectToHost("127.0.0.1", port);
    QTRY_VERIFY_WITH_TIMEOUT(authFailed.count() > 0, 5000);
    QCOMPARE(authFailed.at(0).at(0).value<AuthResult>(), AuthResult::INVALID_CREDENTIALS);
    QVERIFY(authFailed.at(0).at(1).toString().contains(QStringLiteral("认证失败")));
    QVERIFY(!cm.isAuthenticated());
    // 认证被拒是预期结果：以信号承载并断连（同连接重试由上层驱动）
    QTRY_VERIFY_WITH_TIMEOUT(disconnected.count() > 0, 5000);

    cm.disconnectFromHost();
    QTest::qWait(200);
    server.stopServer(true);
    qDeleteAll(m_serverSockets);
    m_serverSockets.clear();
}

QTEST_MAIN(TofuHandshakeTest)
#include "test_tofu_handshake.moc"
