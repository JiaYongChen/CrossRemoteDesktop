#include <QtTest>
#include <QtCore/QTemporaryDir>
#include <QSignalSpy>

#include "client/network/ConnectionManager.h"
#include "client/network/ServerTrustStore.h"
#include "common/config/SettingsManager.h"
#include "common/error/RdError.h"

namespace {
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
private:
    static constexpr const char* kEp = ":0";   // 未调用 connectToHost 时 endpoint 为 ":0"
};

void ConnectionManagerTrustTest::initTestCase() {
    qRegisterMetaType<ConnectionManager::ConnectionState>("ConnectionManager::ConnectionState");
    qRegisterMetaType<RdError>("RdError");
}

void ConnectionManagerTrustTest::firstUseRecordsAndProceeds() {
    QTemporaryDir dir; SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);

    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp1")));

    QVERIFY(sawState(states, ConnectionManager::Connected));
    ServerTrustStore store(sm);
    QCOMPARE(store.verify(kEp, "fp1"), ServerTrustStore::VerifyResult::Trusted);
}

void ConnectionManagerTrustTest::trustedProceeds() {
    QTemporaryDir dir; SettingsManager sm(dir.filePath("c.json")); sm.load();
    ServerTrustStore seed(sm); seed.recordTrust(kEp, "fp1");
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy identity(&cm, &ConnectionManager::serverIdentityChanged);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);

    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp1")));

    QVERIFY(sawState(states, ConnectionManager::Connected));
    QCOMPARE(identity.count(), 0);
}

void ConnectionManagerTrustTest::changedEmitsAndHolds() {
    QTemporaryDir dir; SettingsManager sm(dir.filePath("c.json")); sm.load();
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
    QTemporaryDir dir; SettingsManager sm(dir.filePath("c.json")); sm.load();
    ServerTrustStore seed(sm); seed.recordTrust(kEp, "fp1");
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp2")));

    cm.trustDecision(kEp, "fp2", true);

    QVERIFY(sawState(states, ConnectionManager::Connected));
    ServerTrustStore store(sm);
    QCOMPARE(store.storedFingerprint(kEp), QString("fp2"));   // 已更新为新指纹
}

void ConnectionManagerTrustTest::rejectSurfacesErrorAndKeepsOldTrust() {
    QTemporaryDir dir; SettingsManager sm(dir.filePath("c.json")); sm.load();
    ServerTrustStore seed(sm); seed.recordTrust(kEp, "fp1");
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy errors(&cm, &ConnectionManager::errorOccurred);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp2")));

    cm.trustDecision(kEp, "fp2", false);

    QVERIFY(errors.count() >= 1);
    QVERIFY(errors.last().at(0).value<RdError>().message.contains("拒绝信任"));
    ServerTrustStore store(sm);
    QCOMPARE(store.storedFingerprint(kEp), QString("fp1"));   // 旧信任未被覆盖
    QVERIFY(!sawState(states, ConnectionManager::Connected));
}

void ConnectionManagerTrustTest::decisionIgnoredWhenNotVerifying() {
    QTemporaryDir dir; SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);

    cm.trustDecision("x:1", "fp", true);   // 状态为 Disconnected，应忽略

    QVERIFY(states.isEmpty());
    QVERIFY(sm.trustedHosts().isEmpty());
}

void ConnectionManagerTrustTest::nullStoreBackwardCompatible() {
    ConnectionManager cm(nullptr);   // 未注入 settings → 信任库为空 → 旧行为
    QSignalSpy states(&cm, &ConnectionManager::connectionStateChanged);

    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Q_ARG(QString, "fp")));

    QVERIFY(sawState(states, ConnectionManager::Connected));
}

QTEST_MAIN(ConnectionManagerTrustTest)
#include "test_connectionmanager_trust.moc"
