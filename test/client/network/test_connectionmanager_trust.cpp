#include <QtTest>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QSignalSpy>

#include "client/network/ConnectionManager.h"
#include "client/network/ServerTrustStore.h"
#include "common/config/SettingsManager.h"
#include "common/error/RdError.h"
#include "common/network/Protocol.h"

namespace {
/// 预置空 JSON，使 load() 走解析路径而非迁移路径（迁移会擦除宿主机真实遗留设置）
void seedEmptyConfig(const QString& path) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("{}");
}
/// QSignalSpy 是 QList<QVariantList>，每次触发存一份参数表——需遍历取 at(0) 比较状态
bool sawState(const QSignalSpy& spy, ConnectionManager::ConnectionState want) {
    for (const QVariantList& call : spy) {
        if (call.at(0).value<ConnectionManager::ConnectionState>() == want) {
            return true;
        }
    }
    return false;
}
ConnectionManager::ConnectionState lastState(const QSignalSpy& spy) {
    return spy.last().at(0).value<ConnectionManager::ConnectionState>();
}
} // namespace

class ConnectionManagerTrustTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void firstUseRecordsAndProceeds();
    void trustedProceeds();
    void changedEmitsAndHolds();
    void acceptResumes();
    void rejectSurfacesErrorAndKeepsOldTrust();
    void decisionIgnoredWhenNotVerifying();
    void nullStoreBackwardCompatible();
    void guardDropsHandshakeResponseWhileVerifyingTrust();
    void guardAllowsHandshakeResponseWhenConnected();
    void clipboardDataDroppedUntilAuthenticated();
    void guardDropsMessagesWhileAuthFailed();
    void emptyFingerprintAbortDoesNotArmReconnect();
    void decisionAppliesToLatestPendingFingerprint();
    void handshakeResponseMismatchedAppVersion_emitsVersionMismatch();
    void handshakeResponseMalformedAppVersion_emitsVersionMismatch();
    void handshakeResponseMatchingAppVersion_proceeds();
private:
    static constexpr const char* kEp = ":0";   // 未调用 connectToHost 时 endpoint 为 ":0"
};

void ConnectionManagerTrustTest::initTestCase() {
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    qRegisterMetaType<ConnectionManager::ConnectionState>("ConnectionManager::ConnectionState");
    qRegisterMetaType<RdError>("RdError");
    qRegisterMetaType<MessageType>("MessageType");
}

void ConnectionManagerTrustTest::firstUseRecordsAndProceeds() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);

    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp1")));

    QVERIFY(sawState(states, ConnectionManager::Connected));
    ServerTrustStore store(sm);
    QCOMPARE(store.verify(kEp, "fp1"), ServerTrustStore::VerifyResult::Trusted);
}

void ConnectionManagerTrustTest::trustedProceeds() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ServerTrustStore seed(sm); seed.recordTrust(kEp, "fp1");
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy identity(&cm, &ConnectionManager::serverIdentityChanged);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);

    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp1")));

    QVERIFY(sawState(states, ConnectionManager::Connected));
    QCOMPARE(identity.count(), 0);
}

void ConnectionManagerTrustTest::changedEmitsAndHolds() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ServerTrustStore seed(sm); seed.recordTrust(kEp, "fp1");
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy identity(&cm, &ConnectionManager::serverIdentityChanged);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);

    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp2")));

    QCOMPARE(identity.count(), 1);
    QCOMPARE(identity.at(0).at(0).toString(), QString(kEp));
    QCOMPARE(identity.at(0).at(1).toString(), QString("fp1"));   // 旧指纹
    QCOMPARE(identity.at(0).at(2).toString(), QString("fp2"));   // 新指纹
    QVERIFY(!sawState(states, ConnectionManager::Connected));     // 未放行
    QCOMPARE(lastState(states), ConnectionManager::VerifyingTrust);
}

void ConnectionManagerTrustTest::acceptResumes() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ServerTrustStore seed(sm); seed.recordTrust(kEp, "fp1");
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp2")));

    cm.trustDecision(true);

    QVERIFY(sawState(states, ConnectionManager::Connected));
    ServerTrustStore store(sm);
    QCOMPARE(store.storedFingerprint(kEp), QString("fp2"));   // 已更新为新指纹
}

void ConnectionManagerTrustTest::rejectSurfacesErrorAndKeepsOldTrust() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ServerTrustStore seed(sm); seed.recordTrust(kEp, "fp1");
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy errors(&cm, &ConnectionManager::errorOccurred);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp2")));

    cm.trustDecision(false);

    QVERIFY(errors.count() >= 1);
    QVERIFY(errors.last().at(0).value<RdError>().message.contains("拒绝信任"));
    ServerTrustStore store(sm);
    QCOMPARE(store.storedFingerprint(kEp), QString("fp1"));   // 旧信任未被覆盖
    QVERIFY(!sawState(states, ConnectionManager::Connected));
}

void ConnectionManagerTrustTest::decisionIgnoredWhenNotVerifying() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);

    cm.trustDecision(true);   // 状态为 Disconnected，应忽略

    QVERIFY(states.isEmpty());
    QVERIFY(sm.trustedHosts().isEmpty());
}

void ConnectionManagerTrustTest::nullStoreBackwardCompatible() {
    ConnectionManager cm(nullptr);   // 未注入 settings → 信任库为空 → 旧行为
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);

    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp")));

    QVERIFY(sawState(states, ConnectionManager::Connected));
}

void ConnectionManagerTrustTest::guardDropsHandshakeResponseWhileVerifyingTrust() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ServerTrustStore seed(sm); seed.recordTrust(kEp, "fp1");
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy errors(&cm, &ConnectionManager::errorOccurred);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp2")));   // → VerifyingTrust
    QCOMPARE(lastState(states), ConnectionManager::VerifyingTrust);

    // 未验证服务端在对话框挂起期间推送（畸形）握手响应——应被状态守卫忽略
    const QByteArray forged = QByteArrayLiteral("forged-handshake-response");
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::HANDSHAKE_RESPONSE),
                                      Q_ARG(QByteArray, forged)));

    QCOMPARE(errors.count(), 0);                                  // 未进入 handleHandshakeResponse
    QCOMPARE(lastState(states), ConnectionManager::VerifyingTrust);   // 状态未被改动
}

void ConnectionManagerTrustTest::guardAllowsHandshakeResponseWhenConnected() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy errors(&cm, &ConnectionManager::errorOccurred);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fpNew")));   // FirstUse → Connected
    QVERIFY(sawState(states, ConnectionManager::Connected));

    // Connected（已验证）下畸形握手响应应被正常处理（解码失败 → 报错），证明守卫未过度拦截
    const QByteArray garbage = QByteArrayLiteral("garbage");
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::HANDSHAKE_RESPONSE),
                                      Q_ARG(QByteArray, garbage)));

    QVERIFY(errors.count() >= 1);   // handleHandshakeResponse 解码失败 → errorOccurred
}

void ConnectionManagerTrustTest::guardDropsMessagesWhileAuthFailed() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QSignalSpy messages(&cm, &ConnectionManager::messageReceived);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fpA")));   // FirstUse → Connected

    // 合法路径：服务端拒绝认证 → AuthFailed（连接保活等待同连接重试）
    AuthenticationResponse denied;
    denied.result = AuthResult::INVALID_CREDENTIALS;
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::AUTHENTICATION_RESPONSE),
                                      Q_ARG(QByteArray, denied.encode())));
    QVERIFY(sawState(states, ConnectionManager::AuthFailed));

    // 被拒绝的服务端不得再推送任何消息：伪造"认证成功"不得抬升状态
    AuthenticationResponse fakeOk;
    fakeOk.result = AuthResult::SUCCESS;
    fakeOk.sessionId = QStringLiteral("forged");
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::AUTHENTICATION_RESPONSE),
                                      Q_ARG(QByteArray, fakeOk.encode())));
    QVERIFY(!sawState(states, ConnectionManager::Authenticated));

    // 剪贴板注入同样被忽略（不得转发到 ProtocolSession/本机剪贴板）
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::CLIPBOARD_DATA),
                                      Q_ARG(QByteArray, QByteArrayLiteral("inject"))));
    QCOMPARE(messages.count(), 0);
}

void ConnectionManagerTrustTest::emptyFingerprintAbortDoesNotArmReconnect() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    cm.setAutoReconnect(true);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QSignalSpy errors(&cm, &ConnectionManager::errorOccurred);

    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, QString())));

    QVERIFY(errors.count() >= 1);
    QVERIFY(sawState(states, ConnectionManager::Error));
    QVERIFY(!sawState(states, ConnectionManager::Reconnecting));   // abort 不得武装自动重连
}

void ConnectionManagerTrustTest::clipboardDataDroppedUntilAuthenticated() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy messages(&cm, &ConnectionManager::messageReceived);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fpX")));   // FirstUse → Connected
    QVERIFY(sawState(states, ConnectionManager::Connected));

    // Connected（已验证未授权）：入站剪贴板不得转发到本机剪贴板——
    // FirstUse 静默信任的服务端可在用户授权前注入剪贴板内容
    const QByteArray clip = QByteArrayLiteral("clip-payload");
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::CLIPBOARD_DATA),
                                      Q_ARG(QByteArray, clip)));
    QCOMPARE(messages.count(), 0);

    // 认证成功（伪造合法响应推进状态，仅用于构造 Authenticated 态）
    AuthenticationResponse ok;
    ok.result = AuthResult::SUCCESS;
    ok.sessionId = QStringLiteral("s");
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::AUTHENTICATION_RESPONSE),
                                      Q_ARG(QByteArray, ok.encode())));
    QVERIFY(sawState(states, ConnectionManager::Authenticated));

    // Authenticated：入站剪贴板正常转发
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::CLIPBOARD_DATA),
                                      Q_ARG(QByteArray, clip)));
    QCOMPARE(messages.count(), 1);
    QCOMPARE(messages.at(0).at(0).value<MessageType>(), MessageType::CLIPBOARD_DATA);
}

void ConnectionManagerTrustTest::decisionAppliesToLatestPendingFingerprint() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ServerTrustStore seed(sm); seed.recordTrust(kEp, "fp1");
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy identity(&cm, &ConnectionManager::serverIdentityChanged);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);

    // 第一次变更：挂起待决，对话框将显示 fp2
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp2")));
    QCOMPARE(lastState(states), ConnectionManager::VerifyingTrust);

    // 挂起期间重连且服务端再次换指纹（fp3）：待决上下文必须刷新为最新指纹
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp3")));
    QCOMPARE(identity.count(), 2);
    QCOMPARE(identity.at(1).at(2).toString(), QString("fp3"));
    QCOMPARE(lastState(states), ConnectionManager::VerifyingTrust);

    // 用户确认：记录并重放行的必须是最新待决指纹 fp3，而非过期的 fp2
    cm.trustDecision(true);

    ServerTrustStore store(sm);
    QCOMPARE(store.storedFingerprint(kEp), QString("fp3"));
    QVERIFY(sawState(states, ConnectionManager::Connected));
}

void ConnectionManagerTrustTest::handshakeResponseMismatchedAppVersion_emitsVersionMismatch() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy errors(&cm, &ConnectionManager::errorOccurred);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fpV")));   // FirstUse → Connected
    QVERIFY(sawState(states, ConnectionManager::Connected));

    // 伪造版本不匹配的握手响应（追加后缀构造必然不匹配串，不硬编码版本号）
    HandshakeResponse resp;
    resp.appVersion = QCoreApplication::applicationVersion() + QStringLiteral("-other");
    resp.serverName = QStringLiteral("UltraDesktop Server");
    resp.serverOS = QStringLiteral("Windows");
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::HANDSHAKE_RESPONSE),
                                      Q_ARG(QByteArray, resp.encode())));

    QCOMPARE(errors.count(), 1);
    const RdError err = errors.last().at(0).value<RdError>();
    QCOMPARE(err.code, ErrorCode::Unknown);   // Task 5 重构: 断言 versionMismatched() 信号
    QVERIFY(err.message.contains(QStringLiteral("版本不兼容")));
}

void ConnectionManagerTrustTest::handshakeResponseMalformedAppVersion_emitsVersionMismatch() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy errors(&cm, &ConnectionManager::errorOccurred);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fpW")));
    QVERIFY(sawState(states, ConnectionManager::Connected));

    // 畸形版本串（仅两段）同样必须拒绝
    HandshakeResponse resp;
    resp.appVersion = QStringLiteral("1.0");
    resp.serverName = QStringLiteral("UltraDesktop Server");
    resp.serverOS = QStringLiteral("Windows");
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::HANDSHAKE_RESPONSE),
                                      Q_ARG(QByteArray, resp.encode())));

    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.last().at(0).value<RdError>().code, ErrorCode::Unknown);   // Task 5 重构: 断言 versionMismatched() 信号
}

void ConnectionManagerTrustTest::handshakeResponseMatchingAppVersion_proceeds() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy errors(&cm, &ConnectionManager::errorOccurred);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fpM")));
    QVERIFY(sawState(states, ConnectionManager::Connected));

    // 版本匹配 + saltHex 为空（无密码模式）：正常处理，无错误
    HandshakeResponse resp;
    resp.appVersion = QCoreApplication::applicationVersion();
    resp.serverName = QStringLiteral("UltraDesktop Server");
    resp.serverOS = QStringLiteral("Windows");
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpMessageReceived",
                                      Q_ARG(MessageType, MessageType::HANDSHAKE_RESPONSE),
                                      Q_ARG(QByteArray, resp.encode())));

    QCOMPARE(errors.count(), 0);
}

QTEST_MAIN(ConnectionManagerTrustTest)
#include "test_connectionmanager_trust.moc"
