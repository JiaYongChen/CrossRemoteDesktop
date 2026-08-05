#include <QtTest>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtNetwork/QSslCertificate>
#include <QSignalSpy>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

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

/// ConnectionManager TOFU 信任门（PKI 证书版）：
/// 管道就绪（onTcpConnected）即放行并发射 connected——验证由 TcpClient 层
/// VerifyPeer 完成（真实 TLS 场景见 test_tofu_handshake）；此处验证信任库
/// 记录/读回与空库兼容性。
class ConnectionManagerTrustTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void firstUseProceeds();
    void trustedProceeds();
    void nullStoreBackwardCompatible();
private:
    static constexpr const char* kEp = ":0";   // 未调用 connectToHost 时 endpoint 为 ":0"
};

void ConnectionManagerTrustTest::initTestCase() {
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    qRegisterMetaType<RdError>("RdError");
    qRegisterMetaType<MessageType>("MessageType");
}

void ConnectionManagerTrustTest::firstUseProceeds() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy connectedSpy(&cm, &ConnectionManager::connected);

    // 首连（无信任记录）：VerifyNone 放行 → 管道就绪发射 connected
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected"));

    QCOMPARE(connectedSpy.count(), 1);
    QVERIFY(!cm.isAuthenticated());   // 仅管道就绪，未认证

    // TOFU 记录路径：证书落库后可读回（recordTrust → storedCertificate 往返）
    ServerTrustStore store(sm);
    QVERIFY(!store.storedCertificate(kEp).has_value());   // 首连前无记录
    QByteArray pem;
    QVERIFY(generateTestCertPem(pem));
    const QSslCertificate cert(pem, QSsl::Pem);
    store.recordTrust(kEp, cert);
    const auto stored = store.storedCertificate(kEp);
    QVERIFY(stored.has_value());
    QCOMPARE(QString::fromLatin1(stored->toPem()), QString::fromLatin1(cert.toPem()));
}

void ConnectionManagerTrustTest::trustedProceeds() {
    QTemporaryDir dir; seedEmptyConfig(dir.filePath("c.json")); SettingsManager sm(dir.filePath("c.json")); sm.load();
    ServerTrustStore seed(sm);
    QByteArray pem;
    QVERIFY(generateTestCertPem(pem));
    seed.recordTrust(kEp, QSslCertificate(pem, QSsl::Pem));
    ConnectionManager cm(nullptr, &sm);
    QSignalSpy connectedSpy(&cm, &ConnectionManager::connected);

    // 已信任：连接放行（VerifyPeer 证书匹配验证本身由 TcpClient 集成测试覆盖）
    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected"));

    QCOMPARE(connectedSpy.count(), 1);
}

void ConnectionManagerTrustTest::nullStoreBackwardCompatible() {
    ConnectionManager cm(nullptr);   // 未注入 settings → 信任库为空 → 旧行为
    QSignalSpy connectedSpy(&cm, &ConnectionManager::connected);

    QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected"));

    QCOMPARE(connectedSpy.count(), 1);
}

QTEST_MAIN(ConnectionManagerTrustTest)
#include "test_connectionmanager_trust.moc"
