#include <QtTest>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

#include "client/network/ServerTrustStore.h"
#include "common/config/SettingsManager.h"

namespace {
/// 预置空 JSON，使 load() 走解析路径而非迁移路径（迁移会擦除宿主机真实遗留设置，见可信测试规范）
void seedEmptyConfig(const QString& path) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("{}");
}
} // namespace

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
    void endpointNormalization();
    void recordTrustPersistsWithoutExplicitSave();
    void emptyStoredFingerprintTreatedAsFirstUse();
private:
    QTemporaryDir m_dir;
    SettingsManager* m_sm = nullptr;
    ServerTrustStore* m_store = nullptr;
};

void ServerTrustStoreTest::init() {
    // 每个用例重建干净存储：m_dir 为成员仅构造一次，需删除上一用例残留的配置文件；
    // 随后预置空 JSON，避免 load() 进入遗留迁移分支触碰宿主机生产状态
    QFile::remove(m_dir.filePath("c.json"));
    seedEmptyConfig(m_dir.filePath("c.json"));
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

void ServerTrustStoreTest::endpointNormalization() {
    // IP 字面量规范化、主机名小写化——同一服务端不因拼写差异裂成多个信任条目
    // （裂键会让"换拼写连接"命中 FirstUse 静默信任新证书，绕过变更检测）
    QCOMPARE(ServerTrustStore::endpointFor("FILESERVER", 5921),
             ServerTrustStore::endpointFor("fileserver", 5921));
    QCOMPARE(ServerTrustStore::endpointFor("2001:DB8::1", 5921),
             ServerTrustStore::endpointFor("2001:db8::1", 5921));
    // IP 形式规范化（不止大小写）：同一 IP 的不同文本形式必须归一为同一键，
    // 否则裂键命中 FirstUse 静默信任新证书，绕过变更检测
    QCOMPARE(ServerTrustStore::endpointFor("2001:0db8::0001", 5921),
             ServerTrustStore::endpointFor("2001:db8::1", 5921));
    // 不同端口仍是不同键
    QVERIFY(ServerTrustStore::endpointFor("fileserver", 5921)
            != ServerTrustStore::endpointFor("fileserver", 5922));
    // IP 与主机名不做过度归一（无法证明两者同一台机器）
    QVERIFY(ServerTrustStore::endpointFor("127.0.0.1", 5921)
            != ServerTrustStore::endpointFor("localhost", 5921));
}

void ServerTrustStoreTest::recordTrustPersistsWithoutExplicitSave() {
    // 信任记录是安全攸关状态（丢失 = 静默失去 MITM 变更检测），必须写穿而非仅去抖：
    // 不调用 save()、不自旋事件循环等待去抖定时器，重新加载后条目必须在盘上。
    // 注意 sm 必须在重载期间保持存活——析构函数有 if(m_isModified) save() 兜底，
    // 若先析构会掩盖"非写穿"缺陷
    const QString path = m_dir.filePath("c.json");
    SettingsManager sm(path); sm.load();
    ServerTrustStore store(sm);
    store.recordTrust("h:1", "fp1");

    SettingsManager sm2(path); sm2.load();
    ServerTrustStore store2(sm2);
    QCOMPARE(store2.verify("h:1", "fp1"), ServerTrustStore::VerifyResult::Trusted);
}

void ServerTrustStoreTest::emptyStoredFingerprintTreatedAsFirstUse() {
    // 损坏条目（endpoint 存在但指纹为空）应按首连自愈重录，
    // 而非触发"原指纹为空"的 Changed MITM 告警
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    QJsonArray seeded;
    QJsonObject broken; broken["endpoint"] = "h:1";   // 无 fingerprint 键
    seeded.append(broken);
    sm.setTrustedHosts(seeded);

    ServerTrustStore store(sm);
    QCOMPARE(store.verify("h:1", "fp1"), ServerTrustStore::VerifyResult::FirstUse);
}

QTEST_MAIN(ServerTrustStoreTest)
#include "test_servertruststore.moc"
