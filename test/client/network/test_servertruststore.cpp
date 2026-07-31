#include <QtTest>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

#include "client/network/ServerTrustStore.h"
#include "common/config/SettingsManager.h"

class ServerTrustStoreTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void verifyFirstUseWhenNoRecord();
    void verifyTrustedAfterRecord();
    void verifyChangedOnMismatch();
    void verifyHasNoSideEffects();
    void recordPreservesFirstSeenOnUpdate();
    void storedFingerprintReturnsRecorded();
    void malformedEntriesSkipped();
    void persistsAcrossReload();
private:
    QTemporaryDir m_dir;
    SettingsManager* m_sm = nullptr;
    ServerTrustStore* m_store = nullptr;
};

void ServerTrustStoreTest::init() {
    // 每个用例重建干净存储：m_dir 为成员仅构造一次，需删除上一用例残留的配置文件
    QFile::remove(m_dir.filePath("c.json"));
}

void ServerTrustStoreTest::verifyFirstUseWhenNoRecord() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    ServerTrustStore store(sm);
    QCOMPARE(store.verify("h:1", "fp"), ServerTrustStore::VerifyResult::FirstUse);
}

void ServerTrustStoreTest::verifyTrustedAfterRecord() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    ServerTrustStore store(sm);
    store.recordTrust("h:1", "fp1");
    QCOMPARE(store.verify("h:1", "fp1"), ServerTrustStore::VerifyResult::Trusted);
}

void ServerTrustStoreTest::verifyChangedOnMismatch() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    ServerTrustStore store(sm);
    store.recordTrust("h:1", "fp1");
    QCOMPARE(store.verify("h:1", "fp2"), ServerTrustStore::VerifyResult::Changed);
}

void ServerTrustStoreTest::verifyHasNoSideEffects() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    ServerTrustStore store(sm);
    static_cast<void>(store.verify("h:1", "fp"));   // 纯查询
    QVERIFY(sm.trustedHosts().isEmpty());
    QVERIFY(store.storedFingerprint("h:1").isEmpty());
}

void ServerTrustStoreTest::recordPreservesFirstSeenOnUpdate() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    ServerTrustStore store(sm);
    store.recordTrust("h:1", "fp1");
    const QString firstSeenBefore = sm.trustedHosts().at(0).toObject()["firstSeen"].toString();

    store.recordTrust("h:1", "fp2");   // 覆盖指纹
    const QJsonObject entry = sm.trustedHosts().at(0).toObject();
    QCOMPARE(entry["fingerprint"].toString(), QString("fp2"));
    QCOMPARE(entry["firstSeen"].toString(), firstSeenBefore);   // firstSeen 保留
    QCOMPARE(sm.trustedHosts().size(), 1);                       // 不新增条目
}

void ServerTrustStoreTest::storedFingerprintReturnsRecorded() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    ServerTrustStore store(sm);
    store.recordTrust("h:1", "fp1");
    QCOMPARE(store.storedFingerprint("h:1"), QString("fp1"));
    QVERIFY(store.storedFingerprint("absent:9").isEmpty());
}

void ServerTrustStoreTest::malformedEntriesSkipped() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    QJsonArray seeded;
    seeded.append(QJsonValue("garbage"));          // 非对象条目
    QJsonObject noEndpoint; noEndpoint["fingerprint"] = "x";
    seeded.append(noEndpoint);                      // 缺 endpoint
    QJsonObject valid; valid["endpoint"] = "h:1"; valid["fingerprint"] = "fp1";
    seeded.append(valid);
    sm.setTrustedHosts(seeded);

    ServerTrustStore store(sm);
    QCOMPARE(store.verify("h:1", "fp1"), ServerTrustStore::VerifyResult::Trusted);
    QCOMPARE(store.verify("other:2", "y"), ServerTrustStore::VerifyResult::FirstUse);
}

void ServerTrustStoreTest::persistsAcrossReload() {
    const QString path = m_dir.filePath("c.json");
    {
        SettingsManager sm(path); sm.load();
        ServerTrustStore store(sm);
        store.recordTrust("h:1", "fp1");
        QVERIFY(sm.save());
    }
    SettingsManager sm2(path); sm2.load();
    ServerTrustStore store2(sm2);
    QCOMPARE(store2.verify("h:1", "fp1"), ServerTrustStore::VerifyResult::Trusted);
}

QTEST_MAIN(ServerTrustStoreTest)
#include "test_servertruststore.moc"
