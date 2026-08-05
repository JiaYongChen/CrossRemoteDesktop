#include <QtTest>
#include <QtCore/QFile>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>
#include <QtNetwork/QSslCertificate>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "client/network/ServerTrustStore.h"
#include "common/config/SettingsManager.h"

namespace {
/// 预置空 JSON，使 load() 走解析路径而非迁移路径（迁移会擦除宿主机真实遗留设置，见可信测试规范）
void seedEmptyConfig(const QString& path) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("{}");
}
/// 生成自签名证书 PEM（RSA 密钥随机 → 每次调用互不相同），供信任记录往返测试
bool generateTestCertPem(QByteArray& certPem) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if ( !ctx ) return false;
    if ( EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0 ) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY* pkey = nullptr;
    if ( EVP_PKEY_keygen(ctx, &pkey) <= 0 ) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    X509* x509 = X509_new();
    if ( !x509 ) {
        EVP_PKEY_free(pkey);
        return false;
    }
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), -3600);
    X509_gmtime_adj(X509_get_notAfter(x509), 365L * 24 * 3600);
    X509_set_pubkey(x509, pkey);
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("TestCert"), -1, -1, 0);
    X509_set_issuer_name(x509, name);
    X509_sign(x509, pkey, EVP_sha256());

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, x509);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    certPem = QByteArray(data, static_cast<int>(len));
    BIO_free(bio);

    X509_free(x509);
    EVP_PKEY_free(pkey);
    return true;
}
} // namespace

class ServerTrustStoreTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void storedCertificateNullWhenNoRecord();
    void storedCertificateReturnsRecorded();
    void recordOverwritesCertificate();
    void storedCertificateHasNoSideEffects();
    void malformedEntriesSkipped();
    void persistsAcrossReload();
    void endpointNormalization();
    void recordTrustPersistsWithoutExplicitSave();
    void emptyStoredPemTreatedAsFirstUse();
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

void ServerTrustStoreTest::storedCertificateNullWhenNoRecord() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    ServerTrustStore store(sm);
    QVERIFY(!store.storedCertificate("h:1").has_value());
}

void ServerTrustStoreTest::storedCertificateReturnsRecorded() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    ServerTrustStore store(sm);
    QByteArray pem;
    QVERIFY(generateTestCertPem(pem));
    const QSslCertificate cert(pem, QSsl::Pem);

    store.recordTrust("h:1", cert);
    const auto stored = store.storedCertificate("h:1");
    QVERIFY(stored.has_value());
    QCOMPARE(QString::fromLatin1(stored->toPem()), QString::fromLatin1(cert.toPem()));
    QVERIFY(!store.storedCertificate("absent:9").has_value());
}

void ServerTrustStoreTest::recordOverwritesCertificate() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    ServerTrustStore store(sm);
    QByteArray pemA;
    QByteArray pemB;
    QVERIFY(generateTestCertPem(pemA));
    QVERIFY(generateTestCertPem(pemB));
    const QSslCertificate certA(pemA, QSsl::Pem);
    const QSslCertificate certB(pemB, QSsl::Pem);

    store.recordTrust("h:1", certA);
    store.recordTrust("h:1", certB);   // 覆盖信任（用户确认变更后更新）

    const auto stored = store.storedCertificate("h:1");
    QVERIFY(stored.has_value());
    QCOMPARE(QString::fromLatin1(stored->toPem()), QString::fromLatin1(certB.toPem()));
    QCOMPARE(sm.value("trusted_certs").toJsonObject().size(), 1);   // 不新增条目
}

void ServerTrustStoreTest::storedCertificateHasNoSideEffects() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    ServerTrustStore store(sm);
    static_cast<void>(store.storedCertificate("h:1"));   // 纯查询
    QVERIFY(sm.trustedHosts().isEmpty());
    QVERIFY(sm.value("trusted_certs").toJsonObject().isEmpty());
}

void ServerTrustStoreTest::malformedEntriesSkipped() {
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    // 预置损坏条目：PEM 空串 / 不可解析文本
    QJsonObject broken;
    broken.insert("h:1", QString());
    broken.insert("garbage:2", QStringLiteral("not-a-pem"));
    sm.setValue("trusted_certs", broken);

    ServerTrustStore store(sm);
    QVERIFY(!store.storedCertificate("h:1").has_value());        // 空 PEM → nullopt
    QVERIFY(!store.storedCertificate("garbage:2").has_value());  // 不可解析 → nullopt
    QVERIFY(!store.storedCertificate("absent:3").has_value());   // 无记录 → nullopt
}

void ServerTrustStoreTest::persistsAcrossReload() {
    const QString path = m_dir.filePath("c.json");
    QByteArray pem;
    QVERIFY(generateTestCertPem(pem));
    const QSslCertificate cert(pem, QSsl::Pem);
    {
        SettingsManager sm(path); sm.load();
        ServerTrustStore store(sm);
        store.recordTrust("h:1", cert);
        QVERIFY(sm.save());
    }
    SettingsManager sm2(path); sm2.load();
    ServerTrustStore store2(sm2);
    const auto stored = store2.storedCertificate("h:1");
    QVERIFY(stored.has_value());
    QCOMPARE(QString::fromLatin1(stored->toPem()), QString::fromLatin1(cert.toPem()));
}

void ServerTrustStoreTest::endpointNormalization() {
    // IP 字面量规范化、主机名小写化——同一服务端不因拼写差异裂成多个信任条目
    // （裂键会让"换拼写连接"命中首连路径静默信任新证书，绕过变更检测）
    QCOMPARE(ServerTrustStore::endpointFor("FILESERVER", 5921),
             ServerTrustStore::endpointFor("fileserver", 5921));
    QCOMPARE(ServerTrustStore::endpointFor("2001:DB8::1", 5921),
             ServerTrustStore::endpointFor("2001:db8::1", 5921));
    // IP 形式规范化（不止大小写）：同一 IP 的不同文本形式必须归一为同一键，
    // 否则裂键命中首连路径静默信任新证书，绕过变更检测
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
    QByteArray pem;
    QVERIFY(generateTestCertPem(pem));
    const QSslCertificate cert(pem, QSsl::Pem);
    SettingsManager sm(path); sm.load();
    ServerTrustStore store(sm);
    store.recordTrust("h:1", cert);

    SettingsManager sm2(path); sm2.load();
    ServerTrustStore store2(sm2);
    const auto stored = store2.storedCertificate("h:1");
    QVERIFY(stored.has_value());
    QCOMPARE(QString::fromLatin1(stored->toPem()), QString::fromLatin1(cert.toPem()));
}

void ServerTrustStoreTest::emptyStoredPemTreatedAsFirstUse() {
    // 损坏条目（endpoint 存在但 PEM 为空）应按首连自愈重录，
    // 而非触发"原证书为空"的 Changed MITM 告警
    SettingsManager sm(m_dir.filePath("c.json")); sm.load();
    QJsonObject broken;
    broken.insert("h:1", QString());   // 无 PEM 键值（空串）
    sm.setValue("trusted_certs", broken);

    ServerTrustStore store(sm);
    QVERIFY(!store.storedCertificate("h:1").has_value());
}

QTEST_MAIN(ServerTrustStoreTest)
#include "test_servertruststore.moc"
