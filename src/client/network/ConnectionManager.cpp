#include "ConnectionManager.h"
#include <QtCore/QDebug>
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QMessageLogger>
#include <QtCore/QTimer>
#include "TcpClient.h"
#include "../common/core/config/MessageConstants.h"
#include "../common/core/crypto/Encryption.h"

ConnectionManager::ConnectionManager(QObject* parent)
    : QObject(parent)
    , m_tcpClient(nullptr)
    , m_connectionState(Disconnected)
    , m_currentPort(0)
    , m_connectionTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_autoReconnect(false)
    , m_reconnectInterval(DEFAULT_RECONNECT_INTERVAL)
    , m_maxReconnectAttempts(DEFAULT_MAX_RECONNECT_ATTEMPTS)
    , m_currentReconnectAttempts(0)
    , m_connectionTimeout(CONNECTION_TIMEOUT) {
    setupTcpClient();

    // 设置连接超时定时器
    m_connectionTimer->setSingleShot(true);
    m_connectionTimer->setInterval(m_connectionTimeout);
    connect(m_connectionTimer, &QTimer::timeout, this, &ConnectionManager::onConnectionTimeout);

    // 设置重连定时器
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ConnectionManager::onReconnectTimer);
}

ConnectionManager::~ConnectionManager() {
    cleanupConnection();
}

void ConnectionManager::connectToHost(const QString& host, int port) {
    if ( m_connectionState != Disconnected ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "ConnectionManager: Already connecting or connected, disconnecting first";
        disconnectFromHost();
    }

    m_currentHost = host;
    m_currentPort = port;

    setConnectionState(Connecting);

    // 启动连接超时定时器
    m_connectionTimer->start();

    // 发起连接
    m_tcpClient->connectToHost(host, port);
}

void ConnectionManager::disconnectFromHost() {
    if ( m_connectionState == Disconnected ) {
        return;
    }

    // 停止自动重连
    stopAutoReconnect();
    m_currentReconnectAttempts = 0;

    setConnectionState(Disconnecting);

    // 停止超时定时器
    m_connectionTimer->stop();

    // 断开TCP连接
    if ( m_tcpClient ) {
        m_tcpClient->disconnectFromHost();
    }
}

void ConnectionManager::abort() {
    m_connectionTimer->stop();

    if ( m_tcpClient ) {
        m_tcpClient->abort();
    }

    cleanupConnection();
    setConnectionState(Disconnected);
}

bool ConnectionManager::isConnected() const {
    return m_connectionState == Connected || m_connectionState == Authenticated;
}

bool ConnectionManager::isAuthenticated() const {
    return m_connectionState == Authenticated;
}

QString ConnectionManager::currentHost() const {
    return m_currentHost;
}

int ConnectionManager::currentPort() const {
    return m_currentPort;
}

// 业务逻辑接口实现
void ConnectionManager::authenticate(const QString& username, const QString& password) {
    if ( !isConnected() ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << MessageConstants::Network::NOT_CONNECTED;
        return;
    }

    m_username = username;
    m_password = password;

    sendAuthenticationRequest(username, password);
}

// 自动重连管理方法
void ConnectionManager::setAutoReconnect(bool enable) {
    m_autoReconnect = enable;
    if ( !enable ) {
        stopAutoReconnect();
        m_currentReconnectAttempts = 0;
    }
}

bool ConnectionManager::autoReconnect() const {
    return m_autoReconnect;
}

void ConnectionManager::setReconnectInterval(int msecs) {
    m_reconnectInterval = qMax(1000, msecs); // 最小1秒
}

int ConnectionManager::reconnectInterval() const {
    return m_reconnectInterval;
}

void ConnectionManager::setMaxReconnectAttempts(int attempts) {
    m_maxReconnectAttempts = qMax(0, attempts);
}

int ConnectionManager::maxReconnectAttempts() const {
    return m_maxReconnectAttempts;
}

int ConnectionManager::currentReconnectAttempts() const {
    return m_currentReconnectAttempts;
}

void ConnectionManager::startAutoReconnect() {
    if ( !m_autoReconnect || m_currentReconnectAttempts >= m_maxReconnectAttempts ) {
        return;
    }

    m_currentReconnectAttempts++;
    m_reconnectTimer->setInterval(m_reconnectInterval);
    m_reconnectTimer->start();
}

void ConnectionManager::stopAutoReconnect() {
    m_reconnectTimer->stop();
}

void ConnectionManager::onReconnectTimer() {
    if ( m_connectionState != Disconnected && m_connectionState != Error ) {
        return;
    }

    if ( !m_currentHost.isEmpty() && m_currentPort > 0 ) {
        connectToHost(m_currentHost, m_currentPort);
    }
}

void ConnectionManager::onTcpConnected() {
    m_connectionTimer->stop();
    stopAutoReconnect();
    m_currentReconnectAttempts = 0; // 重置重连计数
    setConnectionState(Connected);

    // 连接成功后发送握手请求
    sendHandshakeRequest();
}

void ConnectionManager::onTcpDisconnected() {
    m_connectionTimer->stop();
    cleanupConnection();

    setConnectionState(Disconnected);

    // 如果启用了自动重连且未达到最大重连次数，则启动重连
    if ( m_autoReconnect && m_currentReconnectAttempts < m_maxReconnectAttempts ) {
        startAutoReconnect();
    } else {
        // 重置重连计数
        m_currentReconnectAttempts = 0;
    }
}

void ConnectionManager::onTcpError(const QString& error) {
    m_connectionTimer->stop();
    setConnectionState(Error);

    // 如果启用了自动重连且未达到最大重连次数，则启动重连
    if ( m_autoReconnect && m_currentReconnectAttempts < m_maxReconnectAttempts ) {
        startAutoReconnect();
    } else {
        // 重置重连计数
        m_currentReconnectAttempts = 0;
    }
}

void ConnectionManager::onConnectionTimeout() {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "ConnectionManager: Connection timeout";
    setConnectionState(Error);

    if ( m_tcpClient ) {
        m_tcpClient->abort();
    }

    // 如果启用了自动重连且未达到最大重连次数，则启动重连
    if ( m_autoReconnect && m_currentReconnectAttempts < m_maxReconnectAttempts ) {
        startAutoReconnect();
    } else {
        // 重置重连计数
        m_currentReconnectAttempts = 0;
    }
}

void ConnectionManager::setConnectionState(ConnectionState state) {
    if ( m_connectionState != state ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << "ConnectionManager: State changed from" << m_connectionState << "to" << state;
        m_connectionState = state;

        // 发射状态变化信号，供 UI 层使用
        emit connectionStateChanged(state);
    }
}

void ConnectionManager::setupTcpClient() {
    m_tcpClient = new TcpClient(this);

    // 连接TCP客户端信号
    connect(m_tcpClient, &TcpClient::connected, this, &ConnectionManager::onTcpConnected);
    connect(m_tcpClient, &TcpClient::disconnected, this, &ConnectionManager::onTcpDisconnected);
    connect(m_tcpClient, &TcpClient::errorOccurred, this, &ConnectionManager::onTcpError);
    connect(m_tcpClient, &TcpClient::messageReceived, this, &ConnectionManager::onTcpMessageReceived);
}

void ConnectionManager::cleanupConnection() {
    m_connectionTimer->stop();
    stopAutoReconnect();
    m_currentReconnectAttempts = 0;

    m_currentHost.clear();
    m_currentPort = 0;
}

// 设置连接超时
void ConnectionManager::setConnectionTimeout(int msecs) {
    // 最小1秒，避免过小值导致误判
    m_connectionTimeout = qMax(1000, msecs);
    if ( m_connectionTimer ) {
        m_connectionTimer->setInterval(m_connectionTimeout);
    }
}

int ConnectionManager::connectionTimeout() const {
    return m_connectionTimeout;
}

// 消息处理 - 只处理连接相关消息，其他转发给上层
void ConnectionManager::onTcpMessageReceived(MessageType type, const QByteArray& payload) {
    switch ( type ) {
        case MessageType::HANDSHAKE_RESPONSE:
            handleHandshakeResponse(payload);
            break;
        case MessageType::AUTHENTICATION_RESPONSE:
            handleAuthenticationResponse(payload);
            break;
        case MessageType::AUTH_CHALLENGE:
            handleAuthChallenge(payload);
            break;
        default:
            // 其他消息转发给上层业务处理
            emit messageReceived(type, payload);
            break;
    }
}

void ConnectionManager::handleHandshakeResponse(const QByteArray& data) {
    HandshakeResponse response;
    if ( response.decode(data) ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient)
            << MessageConstants::Network::HANDSHAKE_RESPONSE_RECEIVED;
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient)
            << "Server version:" << response.serverVersion;
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient)
            << "Screen resolution:" << response.screenWidth << "x" << response.screenHeight;

        // 发送认证请求
        sendAuthenticationRequest(m_username.isEmpty() ? "guest" : m_username,
            m_password.isEmpty() ? "" : m_password);
    } else {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
            << "Failed to parse handshake response";
    }
}

void ConnectionManager::handleAuthenticationResponse(const QByteArray& data) {
    AuthenticationResponse response;
    if ( response.decode(data) ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient)
            << MessageConstants::Network::AUTH_RESPONSE_RECEIVED;
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient)
            << "Auth result:" << static_cast<int>(response.result);

        if ( response.result == AuthResult::SUCCESS ) {
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient)
                << MessageConstants::Network::AUTH_SUCCESSFUL.arg(QString::fromUtf8(response.sessionId));

            stopAutoReconnect();
            m_currentReconnectAttempts = 0;
            setConnectionState(Authenticated);
        } else {
            QString errorMsg;
            switch ( response.result ) {
                case AuthResult::INVALID_PASSWORD:
                    errorMsg = "密码错误";
                    break;
                case AuthResult::ACCESS_DENIED:
                    errorMsg = "访问被拒绝";
                    break;
                case AuthResult::SERVER_FULL:
                    errorMsg = "服务器已满";
                    break;
                default:
                    errorMsg = "认证失败";
                    break;
            }
            setConnectionState(Error);
        }
    } else {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
            << "Failed to parse authentication response";
    }
}

void ConnectionManager::handleAuthChallenge(const QByteArray& data) {
    AuthChallenge ch{};
    if ( ch.decode(data) ) {
        QByteArray salt = QByteArray::fromHex(QByteArray(ch.saltHex));
        if ( !salt.isEmpty() ) {
            // 本地派生 PBKDF2-SHA256
            QByteArray derived = HashGenerator::pbkdf2(m_password.toUtf8(), salt,
                int(ch.iterations), int(ch.keyLength));
            QString hex = derived.toHex();

            // 构造 AuthenticationRequest 并序列化发送
            AuthenticationRequest ar{};
            QByteArray uname = (m_username.isEmpty() ? QString("guest") : m_username).toUtf8();
            int uc = qMin(uname.size(), static_cast<int>(sizeof(ar.username) - 1));
            memcpy(ar.username, uname.constData(), uc);
            ar.username[uc] = '\0';

            QByteArray hexBytes = hex.toUtf8();
            int hc = qMin(hexBytes.size(), static_cast<int>(sizeof(ar.passwordHash) - 1));
            memcpy(ar.passwordHash, hexBytes.constData(), hc);
            ar.passwordHash[hc] = '\0';
            ar.authMethod = 1u;

            m_tcpClient->sendMessage(MessageType::AUTHENTICATION_REQUEST, ar);
        }
    }
}

void ConnectionManager::sendHandshakeRequest() {
    HandshakeRequest request{};
    request.clientVersion = PROTOCOL_VERSION;
    request.screenWidth = 1920;
    request.screenHeight = 1080;
    request.colorDepth = 32;
    strcpy(request.clientName, "QtRemoteDesktop Client");
    strcpy(request.clientOS, getClientOS().toUtf8().constData());

    m_tcpClient->sendMessage(MessageType::HANDSHAKE_REQUEST, request);

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient)
        << MessageConstants::Network::HANDSHAKE_REQUEST_SENT;
}

void ConnectionManager::sendAuthenticationRequest(const QString& username, const QString& password) {
    Q_UNUSED(password);
    // 第一次发送不带hash，触发服务端下发挑战
    AuthenticationRequest ar{};
    QByteArray uname = username.toUtf8();
    int uc = qMin(uname.size(), static_cast<int>(sizeof(ar.username) - 1));
    memcpy(ar.username, uname.constData(), uc);
    ar.username[uc] = '\0';
    ar.passwordHash[0] = '\0';
    ar.authMethod = 1u; // 请求PBKDF2

    m_tcpClient->sendMessage(MessageType::AUTHENTICATION_REQUEST, ar);

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient)
        << MessageConstants::Network::AUTH_REQUEST_SENT.arg(username);
}

QString ConnectionManager::getClientOS() {
#ifdef Q_OS_WIN
    return "Windows";
#elif defined(Q_OS_MAC)
    return "macOS";
#elif defined(Q_OS_LINUX)
    return "Linux";
#else
    return "Unknown";
#endif
}

void ConnectionManager::sendMessage(MessageType type, const IMessageCodec& message) {
    if ( m_tcpClient ) {
        m_tcpClient->sendMessage(type, message);
    }
}
