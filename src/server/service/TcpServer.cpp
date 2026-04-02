#include "TcpServer.h"
#include "../../common/core/network/Protocol.h"
#include "../../common/core/config/NetworkConstants.h"
#include "../simulator/InputSimulator.h"
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QHostAddress>
#include <QtCore/QTimer>
#include <QtCore/QMutexLocker>
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QDateTime>
#include <QtCore/QBuffer>
#include <QtGui/QPixmap>
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/evp.h>

TcpServer::TcpServer(QObject* parent)
    : QTcpServer(parent)
    , m_isRunning(false)
    , m_serverPort(0)
    , m_serverAddress(QHostAddress::Any) {
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
        qCWarning(lcServer) << "Server already running, returning false";
        return false;
    }

    // 生成自签名TLS证书
    if ( m_sslCertificate.isNull() || m_sslPrivateKey.isNull() ) {
        if ( !generateSelfSignedCertificate() ) {
            qCCritical(lcServer) << "Failed to generate TLS certificate";
            emit errorOccurred("Failed to generate TLS certificate");
            return false;
        }
        qCInfo(lcServer) << "TLS self-signed certificate generated successfully";
    }

    m_serverAddress = address;

    // 设置socket选项：允许地址重用（Windows下重要）
    // 这样可以避免TIME_WAIT状态导致的端口占用问题
    setSocketDescriptor(-1); // 重置socket描述符

    if ( !listen(address, port) ) {
        qCWarning(lcServer) << "Failed to start server:" << errorString();
        emit errorOccurred(errorString());
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

        // Windows下需要短暂延迟以确保端口释放
        QCoreApplication::processEvents();
        QThread::msleep(100);

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
    qCDebug(lcNetServer) << "incomingConnection descriptor:" << socketDescriptor;

    // 发出新连接信号，让 ServerManager 处理客户端管理
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
        reinterpret_cast<const unsigned char*>("QtRemoteDesktop"), -1, -1, 0);
    X509_set_issuer_name(x509, name);

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