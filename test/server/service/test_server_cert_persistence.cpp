#include <QtTest>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtNetwork/QSslCertificate>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "common/config/SettingsManager.h"
#include "server/service/TcpServer.h"

namespace {
/// 预置空 JSON，使 load() 走解析路径而非迁移路径（迁移会擦除宿主机真实遗留设置）
void seedEmptyConfig(const QString& path) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("{}");
}
/// 生成自签名证书+私钥 PEM（含 IP SAN），供注入测试使用
/// @param notBeforeOffsetSec notBefore 相对当前的秒偏移；正值 = 尚未生效
/// @param notAfterOffsetSec  notAfter 相对当前的秒偏移；负值 = 已过期
bool generateTestCertPem(QByteArray& certPem, QByteArray& keyPem,
                         long notBeforeOffsetSec, long notAfterOffsetSec) {
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
    X509_gmtime_adj(X509_get_notBefore(x509), notBeforeOffsetSec);
    X509_gmtime_adj(X509_get_notAfter(x509), notAfterOffsetSec);
    X509_set_pubkey(x509, pkey);
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("TestCert"), -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // SAN：IP:127.0.0.1——让注入证书通过加载侧的 SAN 存在性检查，
    // 使过期/配对测试只检验各自的目标条件
    GENERAL_NAMES* altNames = GENERAL_NAMES_new();
    GENERAL_NAME* entry = altNames ? GENERAL_NAME_new() : nullptr;
    ASN1_OCTET_STRING* ip = entry ? a2i_IPADDRESS("127.0.0.1") : nullptr;
    if ( altNames && entry && ip ) {
        entry->type = GEN_IPADD;
        entry->d.iPAddress = ip;
        sk_GENERAL_NAME_push(altNames, entry);
        X509_EXTENSION* ext = X509V3_EXT_i2d(NID_subject_alt_name, 0, altNames);
        if ( ext ) {
            X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
        }
    }
    GENERAL_NAMES_free(altNames);

    X509_sign(x509, pkey, EVP_sha256());

    BIO* certBio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(certBio, x509);
    char* data = nullptr;
    long len = BIO_get_mem_data(certBio, &data);
    certPem = QByteArray(data, static_cast<int>(len));
    BIO_free(certBio);

    BIO* keyBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    len = BIO_get_mem_data(keyBio, &data);
    keyPem = QByteArray(data, static_cast<int>(len));
    BIO_free(keyBio);

    X509_free(x509);
    EVP_PKEY_free(pkey);
    return true;
}
} // namespace

class ServerCertPersistenceTest : public QObject {
    Q_OBJECT
private slots:
    void certificateStableAcrossRestart();
    void certificatePersistedWithoutExplicitSave();
    void expiredCertificateTriggersRegeneration();
    void mismatchedKeyTriggersRegeneration();
    void notYetValidCertificateTriggersRegeneration();
};

void ServerCertPersistenceTest::certificateStableAcrossRestart() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cfg = dir.filePath("config.json");
    seedEmptyConfig(cfg);

    QByteArray fp1;
    {
        SettingsManager sm(cfg);
        sm.load();
        TcpServer server(nullptr, &sm);
        QVERIFY(server.startServer(0));
        fp1 = server.sslCertificate().digest(QCryptographicHash::Sha256);
        server.stopServer(true);
        QVERIFY(sm.save());
    }

    QByteArray fp2;
    {
        SettingsManager sm(cfg);
        sm.load();
        TcpServer server(nullptr, &sm);
        QVERIFY(server.startServer(0));
        fp2 = server.sslCertificate().digest(QCryptographicHash::Sha256);
        server.stopServer(true);
    }

    QVERIFY(!fp1.isEmpty());
    QCOMPARE(fp1, fp2);   // 重启后复用同一证书——TOFU 基石断言
}

void ServerCertPersistenceTest::certificatePersistedWithoutExplicitSave() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cfg = dir.filePath("config.json");
    seedEmptyConfig(cfg);

    SettingsManager sm(cfg);
    sm.load();
    {
        TcpServer server(nullptr, &sm);
        QVERIFY(server.startServer(0));
        server.stopServer(true);
    }
    // 不调用 save()，也不自旋事件循环等待去抖定时器：
    // 证书是关键安全状态，必须在生成时同步写穿——否则应用在任一次主线程保存前被强杀
    // 即丢失证书 → 下次启动重新生成 → 所有客户端收到虚假"身份变更"警告

    SettingsManager probe(cfg);
    probe.load();
    QVERIFY(!probe.getString("Server/tlsCertPem").isEmpty());
    QVERIFY(!probe.getString("Server/tlsKeyPem").isEmpty());
}

void ServerCertPersistenceTest::expiredCertificateTriggersRegeneration() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cfg = dir.filePath("config.json");
    seedEmptyConfig(cfg);
    QByteArray expiredCert;
    QByteArray expiredKey;
    QVERIFY(generateTestCertPem(expiredCert, expiredKey, -7200, -3600));   // 已过期 1 小时

    SettingsManager sm(cfg);
    sm.load();
    sm.setValue("Server/tlsCertPem", QString::fromUtf8(expiredCert));
    sm.setValue("Server/tlsKeyPem", QString::fromUtf8(expiredKey));
    QVERIFY(sm.save());

    TcpServer server(nullptr, &sm);
    QVERIFY(server.startServer(0));
    // 过期证书必须被拒绝复用：重新生成 → 指纹不同于过期证书
    // （客户端把 CertificateExpired 当致命错误，过期证书被加载即全体客户端握手失败且无自愈）
    const QSslCertificate expired(expiredCert, QSsl::Pem);
    QVERIFY(server.sslCertificate().digest(QCryptographicHash::Sha256)
            != expired.digest(QCryptographicHash::Sha256));
    server.stopServer(true);
}

void ServerCertPersistenceTest::mismatchedKeyTriggersRegeneration() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cfg = dir.filePath("config.json");
    seedEmptyConfig(cfg);
    QByteArray cert1;
    QByteArray key1;
    QByteArray cert2;
    QByteArray key2;
    // 有效期必须远超 30 天过期闸门（90 天），确保真正到达配对校验分支而非被过期检查先截胡
    QVERIFY(generateTestCertPem(cert1, key1, -3600, 90L * 24 * 3600));
    QVERIFY(generateTestCertPem(cert2, key2, -3600, 90L * 24 * 3600));

    SettingsManager sm(cfg);
    sm.load();
    sm.setValue("Server/tlsCertPem", QString::fromUtf8(cert1));   // 证书来自批次 1
    sm.setValue("Server/tlsKeyPem", QString::fromUtf8(key2));     // 私钥来自批次 2——错配
    QVERIFY(sm.save());

    TcpServer server(nullptr, &sm);
    QVERIFY(server.startServer(0));
    // 错配对不得被接受（接受后服务端握手必然失败且无自愈）→ 重新生成
    const QSslCertificate c1(cert1, QSsl::Pem);
    QVERIFY(server.sslCertificate().digest(QCryptographicHash::Sha256)
            != c1.digest(QCryptographicHash::Sha256));
    server.stopServer(true);
}

void ServerCertPersistenceTest::notYetValidCertificateTriggersRegeneration() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cfg = dir.filePath("config.json");
    seedEmptyConfig(cfg);
    QByteArray futureCert;
    QByteArray futureKey;
    // notBefore 在未来 1 小时（时钟回拨/便携拷贝场景），有效期远、SAN/配对均正常
    QVERIFY(generateTestCertPem(futureCert, futureKey, 3600, 365L * 24 * 3600));

    SettingsManager sm(cfg);
    sm.load();
    sm.setValue("Server/tlsCertPem", QString::fromUtf8(futureCert));
    sm.setValue("Server/tlsKeyPem", QString::fromUtf8(futureKey));
    QVERIFY(sm.save());

    TcpServer server(nullptr, &sm);
    QVERIFY(server.startServer(0));
    // 尚未生效的证书必须被拒绝复用：重新生成 → 指纹不同于注入证书
    // （客户端把 CertificateNotYetValid 当致命错误，加载即全体握手失败且无自愈）
    const QSslCertificate future(futureCert, QSsl::Pem);
    QVERIFY(server.sslCertificate().digest(QCryptographicHash::Sha256)
            != future.digest(QCryptographicHash::Sha256));
    server.stopServer(true);
}

QTEST_MAIN(ServerCertPersistenceTest)
#include "test_server_cert_persistence.moc"
