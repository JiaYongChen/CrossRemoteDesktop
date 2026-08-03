#include "server/service/TcpServer.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QSet>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QHostInfo>
#include <QtNetwork/QNetworkInterface>

#include "common/config/SettingsManager.h"
#include "common/error/RdError.h"
#include "common/logging/LoggingCategories.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

TcpServer::TcpServer(QObject* parent, SettingsManager* settings)
    : QTcpServer(parent)
    , m_isRunning(false)
    , m_serverPort(0)
    , m_serverAddress(QHostAddress::Any)
    , m_settings(settings) {
    // 基础服务器初始化
}

TcpServer::~TcpServer() {
    qCDebug(lcServer) << "TcpServer destructor called";

    // 析构时使用同步停止，确保资源完全释放
    if ( m_isRunning ) {
        stopServer(true); // 同步停止
    }

    qCDebug(lcServer) << "TcpServer destructor completed";
}

bool TcpServer::startServer(quint16 port, const QHostAddress& address) {
    qCInfo(lcServer) << "TcpServer::startServer() called with port:" << port << "address:" << address.toString();

    if ( m_isRunning ) {
        qCDebug(lcServer) << "Server already running, returning false";
        return false;
    }

    // 获取/生成自签名TLS证书：优先加载持久化证书，缺失或损坏才重新生成并落盘
    if ( m_sslCertificate.isNull() || m_sslPrivateKey.isNull() ) {
        bool ready = false;
        if ( m_settings ) {
            ready = loadPersistedCertificate();
            if ( ready ) {
                qCInfo(lcServer) << "TLS certificate loaded from persistent storage";
            }
        }
        if ( !ready ) {
            if ( !generateSelfSignedCertificate() ) {
                qCCritical(lcServer) << "Failed to generate TLS certificate";
                emit errorOccurred(RdError(ErrorCode::NetworkTlsError, "Failed to generate TLS certificate", "TcpServer"));
                return false;
            }
            qCDebug(lcServer) << "TLS self-signed certificate generated successfully";
            if ( m_settings ) {
                persistCertificate();
            }
        }
    }

    m_serverAddress = address;

    // 设置socket选项：允许地址重用（Windows下重要）
    // 这样可以避免TIME_WAIT状态导致的端口占用问题
    setSocketDescriptor(-1); // 重置socket描述符

    if ( !listen(address, port) ) {
        qCWarning(lcServer) << "Failed to start server:" << errorString();
        emit errorOccurred(RdError(ErrorCode::TcpListenerError, errorString(), "TcpServer"));
        return false;
    }

    // 获取实际监听的端口（当传入端口为0时，系统会自动分配）
    m_serverPort = QTcpServer::serverPort();

    qCInfo(lcServer) << "Server successfully started on port:" << m_serverPort
        << "address:" << serverAddress().toString()
        << "listening:" << isListening()
        << "maxPending:" << maxPendingConnections();
    m_isRunning = true;
    return true;
}

void TcpServer::stopServer() {
    stopServer(false); // 默认异步停止
}

void TcpServer::stopServer(bool synchronous) {
    if ( !m_isRunning ) {
        return;
    }

    qCInfo(lcServer) << "Stopping server, synchronous:" << synchronous;

    auto cleanup = [this]() {
        qCDebug(lcServer) << "Starting server cleanup...";

        // 1. 停止接受新连接
        pauseAccepting();

        // 2. 关闭服务器监听
        close();

        // 3. 强制刷新网络缓冲区（Windows特定）
        QCoreApplication::processEvents();

        // 4. 短暂延迟确保系统释放端口（Windows下TIME_WAIT状态）
        QThread::msleep(100);

        m_isRunning = false;
        qCInfo(lcServer) << "Server stopped successfully, port should be released";
        emit serverStopped();
    };

    if ( synchronous ) {
        // 同步执行清理操作，用于应用程序关闭时
        pauseAccepting();
        close();
        m_isRunning = false;
        qCInfo(lcServer) << "Server stopped synchronously, port released";
        emit serverStopped();
    } else {
        // 使用定时器异步执行清理操作，避免阻塞UI线程
        QTimer::singleShot(0, this, cleanup);
    }
}

bool TcpServer::isRunning() const {
    return m_isRunning;
}

quint16 TcpServer::serverPort() const {
    return m_serverPort;
}

QHostAddress TcpServer::serverAddress() const {
    return m_serverAddress;
}

void TcpServer::incomingConnection(qintptr socketDescriptor) {
    qCDebug(lcServerNetwork) << "incomingConnection descriptor:" << socketDescriptor;

    // 发出新连接信号，让 TcpListener 处理
    emit newClientConnection(socketDescriptor);
}

bool TcpServer::generateSelfSignedCertificate() {
    // 使用OpenSSL 3.0+ EVP API生成RSA-2048自签名证书
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if ( !ctx ) return false;

    if ( EVP_PKEY_keygen_init(ctx) <= 0 ) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    if ( EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0 ) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    EVP_PKEY* pkey = nullptr;
    if ( EVP_PKEY_keygen(ctx, &pkey) <= 0 ) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    // 创建X509证书
    X509* x509 = X509_new();
    if ( !x509 ) {
        EVP_PKEY_free(pkey);
        return false;
    }

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 365 * 24 * 3600); // 1年有效期

    X509_set_pubkey(x509, pkey);

    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("CrossRemoteDesktop"), -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // ── Subject Alternative Name（SAN）──
    // Qt 的 TLS 客户端在 client 模式握手中无条件做主机名校验（Qt 6.9 不受 VerifyNone
    // 影响且无关闭开关）：证书必须含可匹配的 SAN 才能从根源消除 HostNameMismatch。
    // TOFU 身份仍是指纹——SAN 只用于消除校验噪声，不参与信任决策。
    {
        GENERAL_NAMES* altNames = GENERAL_NAMES_new();
        if ( altNames ) {
            const auto addDnsName = [&altNames](const QString& dnsName) {
                const QByteArray utf8 = dnsName.toUtf8();
                if ( utf8.isEmpty() ) {
                    return;
                }
                GENERAL_NAME* entry = GENERAL_NAME_new();
                entry->type = GEN_DNS;
                ASN1_IA5STRING* ia5 = ASN1_IA5STRING_new();
                ASN1_STRING_set(ia5, utf8.constData(), utf8.size());
                entry->d.dNSName = ia5;
                sk_GENERAL_NAME_push(altNames, entry);
            };
            const auto addIpAddress = [&altNames](const QHostAddress& addr) {
                const QByteArray text = addr.toString().toUtf8();
                ASN1_OCTET_STRING* ip = a2i_IPADDRESS(text.constData());
                if ( !ip ) {
                    return;
                }
                GENERAL_NAME* entry = GENERAL_NAME_new();
                entry->type = GEN_IPADD;
                entry->d.iPAddress = ip;
                sk_GENERAL_NAME_push(altNames, entry);
            };

            // DNS：localhost + 本机名（覆盖按名字连接）
            addDnsName(QStringLiteral("localhost"));
            addDnsName(QHostInfo::localHostName());

            // IP：环回 + 生成时的全部本机地址（覆盖按 IP 连接；IP 变化后回退为忽略预期错误）
            QList<QHostAddress> ips;
            ips << QHostAddress(QHostAddress::LocalHost) << QHostAddress(QHostAddress::LocalHostIPv6);
            const QList<QHostAddress> allAddresses = QNetworkInterface::allAddresses();
            for ( const QHostAddress& addr : allAddresses ) {
                if ( addr.isNull() || addr.isMulticast() ) {
                    continue;
                }
                ips << addr;
            }
            QSet<QString> seen;
            for ( const QHostAddress& addr : ips ) {
                const QString text = addr.toString();
                if ( seen.contains(text) ) {
                    continue;
                }
                seen.insert(text);
                addIpAddress(addr);
            }

            X509_EXTENSION* ext = X509V3_EXT_i2d(NID_subject_alt_name, 0, altNames);
            if ( ext ) {
                X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
            }
            GENERAL_NAMES_free(altNames);
        }
    }

    X509_sign(x509, pkey, EVP_sha256());

    // 导出证书为PEM
    BIO* certBio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(certBio, x509);
    char* certData = nullptr;
    long certLen = BIO_get_mem_data(certBio, &certData);
    QByteArray certPem(certData, static_cast<int>(certLen));
    BIO_free(certBio);

    // 导出私钥为PEM
    BIO* keyBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* keyData = nullptr;
    long keyLen = BIO_get_mem_data(keyBio, &keyData);
    QByteArray keyPem(keyData, static_cast<int>(keyLen));
    BIO_free(keyBio);

    X509_free(x509);
    EVP_PKEY_free(pkey);

    // 转换为Qt类型
    m_sslCertificate = QSslCertificate(certPem, QSsl::Pem);
    m_sslPrivateKey = QSslKey(keyPem, QSsl::Rsa, QSsl::Pem);

    if ( m_sslCertificate.isNull() || m_sslPrivateKey.isNull() ) {
        qCCritical(lcServer) << "Failed to parse generated certificate or key";
        return false;
    }

    return true;
}

bool TcpServer::loadPersistedCertificate() {
    const QByteArray certPem = m_settings->getString("Server/tlsCertPem").toUtf8();
    const QByteArray keyPem  = m_settings->getString("Server/tlsKeyPem").toUtf8();
    if ( certPem.isEmpty() || keyPem.isEmpty() ) {
        return false;
    }

    QSslCertificate cert(certPem, QSsl::Pem);
    QSslKey key(keyPem, QSsl::Rsa, QSsl::Pem);
    if ( cert.isNull() || key.isNull() ) {
        qCWarning(lcServer) << "Persisted TLS certificate/key unparseable, will regenerate";
        return false;
    }

    // 旧版生成的证书无 SAN，按主机名连接会触发 Qt 的 HostNameMismatch 校验噪声——
    // 视为过期并重新生成（指纹变化将使客户端收到一次 TOFU 变更确认，属预期）
    if ( cert.subjectAlternativeNames().isEmpty() ) {
        qCInfo(lcServer) << "Persisted TLS certificate has no SAN, will regenerate";
        return false;
    }

    m_sslCertificate = cert;
    m_sslPrivateKey = key;
    return true;
}

void TcpServer::persistCertificate() {
    m_settings->setValue("Server/tlsCertPem", QString::fromUtf8(m_sslCertificate.toPem()));
    m_settings->setValue("Server/tlsKeyPem", QString::fromUtf8(m_sslPrivateKey.toPem()));
    // 证书是关键安全状态，同步写穿，不依赖去抖定时器：
    // 丢失证书意味着下次启动重新生成，所有客户端将收到虚假"身份变更"MITM 警告
    m_settings->save();
}
