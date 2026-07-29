#include "ConnectionManager.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QTimer>
#include <QtNetwork/QPasswordDigestor>

#include "client/network/TcpClient.h"
#include "common/config/NetworkConstants.h"
#include "common/config/ProtocolConstants.h"
#include "common/error/RdError.h"
#include "common/logging/LoggingCategories.h"

ConnectionManager::ConnectionManager(QObject* parent)
    : QObject(parent)
    , m_tcpClient(nullptr)
    , m_connectionState(Disconnected)
    , m_currentPort(0)
    , m_connectionTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_autoReconnect(false)
    , m_reconnectInterval(NetworkConstants::DefaultReconnectInterval)
    , m_maxReconnectAttempts(NetworkConstants::DefaultMaxReconnectAttempts)
    , m_currentReconnectAttempts(0)
    , m_connectionTimeout(NetworkConstants::DefaultConnectionTimeout) {
    setupTcpClient();

    m_connectionTimer->setSingleShot(true);
    m_connectionTimer->setInterval(m_connectionTimeout);
    connect(m_connectionTimer, &QTimer::timeout, this, &ConnectionManager::onConnectionTimeout);

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ConnectionManager::onReconnectTimer);
}

ConnectionManager::~ConnectionManager() {
    m_connectionTimer->stop();
    m_reconnectTimer->stop();

    if ( m_tcpClient ) {
        m_tcpClient->disconnect();
    }

    cleanupConnection();
}

// ═══════════════════════════════════════════════════════════════════
// 连接控制
// ═══════════════════════════════════════════════════════════════════

void ConnectionManager::connectToHost(const QString& host, int port) {
    // 缓存连接目标，供 retryAuthentication 使用
    m_host = host;
    m_port = port;

    // 非 Disconnected/Reconnecting/AuthFailed → 同步硬断旧连接
    if ( m_connectionState != Disconnected
         && m_connectionState != Reconnecting
         && m_connectionState != AuthFailed ) {
        disconnectFromHost();         // 停定时器 + 置 Disconnecting + 置标记
        if ( m_tcpClient ) {
            m_tcpClient->abort();    // 同步硬断，不发信号，标记由 onTcpDisconnected 消费
        }
    }

    // AuthFailed 终态重试：服务端 FIN 可能尚未到达，需显式强制重置 socket 到 UnconnectedState
    if ( m_connectionState == AuthFailed ) {
        if ( m_tcpClient && m_tcpClient->isConnected() ) {
            m_tcpClient->abort();
        }
    }

    // 显式连接请求重置预算
    m_currentReconnectAttempts = 0;

    startConnection(host, port);
}

void ConnectionManager::disconnectFromHost() {
    stopAutoReconnect();

    if ( m_connectionState == Disconnected ) {
        return;
    }

    // Error/AuthFailed 态 + socket 已死 → 无 disconnected 信号会来，直接收敛
    if ( m_tcpClient && !m_tcpClient->isConnected()
         && (m_connectionState == Error || m_connectionState == AuthFailed) ) {
        m_currentReconnectAttempts = 0;
        cleanupConnection();
        setConnectionState(Disconnected);
        return;
    }

    m_currentReconnectAttempts = 0;
    setConnectionState(Disconnecting);
    m_connectionTimer->stop();

    // 仅在实际将产生异步 disconnected 信号时置标记（提前返回路径已排除）
    m_userInitiatedDisconnect = true;

    if ( m_tcpClient ) {
        m_tcpClient->disconnectFromHost();
    }
}

void ConnectionManager::startConnection(const QString& host, int port) {
    m_currentHost = host;
    m_currentPort = port;

    setConnectionState(Connecting);
    m_connectionTimer->start();
    m_tcpClient->connectToHost(host, port);
}

// ═══════════════════════════════════════════════════════════════════
// 状态查询
// ═══════════════════════════════════════════════════════════════════

bool ConnectionManager::isConnected() const {
    if ( !m_tcpClient ) return false;
    return m_tcpClient->isConnected();
}

bool ConnectionManager::isAuthenticated() const {
    return m_connectionState == Authenticated;
}

// ═══════════════════════════════════════════════════════════════════
// 配置
// ═══════════════════════════════════════════════════════════════════

void ConnectionManager::setCredentials(const QString& username, const QString& password) {
    m_username = username;
    m_password = password;
}

void ConnectionManager::setColorDepth(int depth) {
    m_colorDepth = qBound(16, depth, 32);
}

void ConnectionManager::setImageQuality(int quality) {
    m_imageQuality = qBound(1, quality, 100);
}

void ConnectionManager::setAutoReconnect(bool enable) {
    m_autoReconnect = enable;
    if ( !enable ) {
        stopAutoReconnect();
        m_currentReconnectAttempts = 0;
    }
}

void ConnectionManager::setReconnectInterval(int msecs) {
    m_reconnectInterval = qMax(1000, msecs);
}

void ConnectionManager::setConnectionTimeout(int msecs) {
    m_connectionTimeout = qMax(1000, msecs);
    if ( m_connectionTimer ) {
        m_connectionTimer->setInterval(m_connectionTimeout);
    }
}

// ═══════════════════════════════════════════════════════════════════
// 自动重连 — FSM 单一权威入口（调用方不预置状态，以返回值驱动）
// ═══════════════════════════════════════════════════════════════════

bool ConnectionManager::startAutoReconnect() {
    if ( !m_autoReconnect || m_currentReconnectAttempts >= m_maxReconnectAttempts ) {
        return false;
    }

    // 已有待触发定时器：不重复计数（error+disconnected 双路径去重）
    if ( m_reconnectTimer->isActive() ) {
        return true;
    }

    m_currentReconnectAttempts++;
    m_reconnectTimer->setInterval(m_reconnectInterval);
    m_reconnectTimer->start();
    setConnectionState(Reconnecting);
    return true;
}

void ConnectionManager::stopAutoReconnect() {
    m_reconnectTimer->stop();
}

// ═══════════════════════════════════════════════════════════════════
// 事件处理
// ═══════════════════════════════════════════════════════════════════

void ConnectionManager::onTcpConnected() {
    m_connectionTimer->stop();
    stopAutoReconnect();
    // 不在此清零计数——TCP 成功 ≠ 认证成功，预算只在用户 connectToHost 和认证成功时复位
    setConnectionState(Connected);
    sendHandshakeRequest();
}

void ConnectionManager::onTcpDisconnected() {
    m_connectionTimer->stop();

    // ── 重入守卫：在 onTcpError/onConnectionTimeout 内部由 abort() 触发时，
    // 不独立做状态决策——由外层错误/超时处理函数统一负责重连/清理。
    if ( m_handlingError ) {
        return;
    }

    // ── 分支 1：用户主动断开 ──
    if ( m_userInitiatedDisconnect ) {
        m_userInitiatedDisconnect = false;
        m_currentReconnectAttempts = 0;
        setConnectionState(Disconnected);
        cleanupConnection();
        return;
    }

    // ── 分支 2：AuthFailed 永久终端态 → 收敛 Disconnected ──
    if ( m_connectionState == AuthFailed ) {
        setConnectionState(Disconnected);
        cleanupConnection();
        return;
    }

    // ── 分支 3：意外断线 — startAutoReconnect 单一决策点 ──
    // 调用方不预置状态：startAutoReconnect 成功返回则状态已为 Reconnecting，
    // 失败（!autoReconnect || 预算耗尽）则收敛 Disconnected。
    // 若定时器已活跃（前置 onTcpError 已武装），返回 true 且只修正状态不重复计数。
    if ( !startAutoReconnect() ) {
        // autoReconnect=false（默认）或预算耗尽或 AuthFailed 已在此→Disconnected
        setConnectionState(Disconnected);
        cleanupConnection();
    }
    // 成功路径：状态已由 startAutoReconnect 置为 Reconnecting
}

void ConnectionManager::onTcpError(const RdError& error) {
    if ( m_userInitiatedDisconnect ) {
        m_connectionTimer->stop();
        return;
    }

    // AuthFailed 态收到 error 是预期内（服务端在拒绝认证后关闭连接）——静默
    if ( m_connectionState == AuthFailed ) {
        m_connectionTimer->stop();
        return;
    }

    // 通过守卫检查后转发 TcpClient 已翻译的详细错误信息到上层（含中文诊断）
    emit errorOccurred(error);

    m_connectionTimer->stop();

    // 确保 socket 回到 UnconnectedState，否则重连时 TcpClient::connectToHost()
    // 会因状态检查失败而静默返回（SocketTimeoutError 等错误不自动转换状态）。
    // m_handlingError 阻止 abort() 同步触发的 onTcpDisconnected 独立决策
    // （其 cleanupConnection 会重置计数器+清除 host/port，干扰本函数的重连决策）。
    m_handlingError = true;
    if ( m_tcpClient ) {
        m_tcpClient->abort();
    }
    m_handlingError = false;

    if ( !startAutoReconnect() ) {
        setConnectionState(Error);
        m_currentReconnectAttempts = 0;
    }
}

void ConnectionManager::onConnectionTimeout() {
    qCWarning(lcClient) << "ConnectionManager: Connection timeout";

    if ( m_userInitiatedDisconnect ) {
        m_connectionTimer->stop();
        return;
    }

    m_connectionTimer->stop();

    // 同 onTcpError：abort() 同步触发 onTcpDisconnected，需守卫防止其
    // cleanupConnection 重置计数器+清除 host/port 干扰本函数的重连决策。
    m_handlingError = true;
    if ( m_tcpClient ) {
        m_tcpClient->abort();
    }
    m_handlingError = false;

    if ( !startAutoReconnect() ) {
        // 连接超时且不重连：发射具体错误消息（替代 wireSignals 中的通用兜底）
        emit errorOccurred(RdError(ErrorCode::NetworkConnectionFailed,
            QStringLiteral("连接超时"), "ConnectionManager"));
        setConnectionState(Error);
        m_currentReconnectAttempts = 0;
    }
}

void ConnectionManager::onReconnectTimer() {
    if ( m_connectionState != Reconnecting ) {
        return;
    }

    if ( !m_currentHost.isEmpty() && m_currentPort > 0 ) {
        startConnection(m_currentHost, m_currentPort);
    }
}

// ═══════════════════════════════════════════════════════════════════
// 状态管理
// ═══════════════════════════════════════════════════════════════════

void ConnectionManager::setConnectionState(ConnectionState state) {
    if ( m_connectionState != state ) {
        qCInfo(lcClient) << "ConnectionManager: State changed from" << m_connectionState << "to" << state;
        m_connectionState = state;
        emit connectionStateChanged(state);
    }
}

void ConnectionManager::setupTcpClient() {
    m_tcpClient = new TcpClient(this);

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

// ═══════════════════════════════════════════════════════════════════
// 消息处理
// ═══════════════════════════════════════════════════════════════════

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
            emit messageReceived(type, payload);
            break;
    }
}

void ConnectionManager::handleHandshakeResponse(const QByteArray& data) {
    HandshakeResponse response;
    if ( response.decode(data) ) {
        qCDebug(lcClient) << "Received handshake response from server";
        // 不再主动发送认证请求——等待服务端主导后续步骤（下发 Challenge 或直接通过认证）
    } else {
        const RdError error(ErrorCode::DecodeFailed,
                            tr("握手响应解码失败"), "ConnectionManager");
        qCWarning(lcClient) << error.logLabel();
        emit errorOccurred(error);
        disconnectFromHost();
    }
}

void ConnectionManager::handleAuthenticationResponse(const QByteArray& data) {
    AuthenticationResponse response;
    if ( response.decode(data) ) {
        if ( response.result == AuthResult::SUCCESS ) {
            qCInfo(lcClient) << "Authentication successful, session ID:" << response.sessionId;
            m_connectionTimer->stop();
            stopAutoReconnect();
            m_currentReconnectAttempts = 0;
            setConnectionState(Authenticated);
            sendSessionCapabilities();   // 认证成功后告知服务端编码偏好
        } else {
            QString reason;
            ErrorCode code;
            switch ( response.result ) {
                case AuthResult::INVALID_USERNAME:
                    reason = tr("认证失败：用户名无效");
                    code = ErrorCode::AuthInvalidUsername;
                    break;
                case AuthResult::INVALID_PASSWORD:
                    reason = tr("认证失败：密码错误");
                    code = ErrorCode::AuthInvalidPassword;
                    break;
                case AuthResult::ACCESS_DENIED:
                    reason = tr("认证失败：尝试次数过多，请稍后重试");
                    code = ErrorCode::AuthAccessDenied;
                    break;
                default:
                    reason = tr("认证失败");
                    code = ErrorCode::AuthAccessDenied;
                    break;
            }
            const RdError error(code, reason, "ConnectionManager");
            qCWarning(lcClient) << error.logLabel();
            emit errorOccurred(error);
            setConnectionState(AuthFailed);
            stopAutoReconnect();

            if (code != ErrorCode::AuthAccessDenied) {
                // 可重试错误：保留密码明文用于重试对话框预填
                // 不主动断开——等服务端关闭连接或重试时 abort
            } else {
                // 终态错误：清除密码，主动断开
                m_password.fill(QChar(0));
                m_password.clear();
                disconnectFromHost();
            }
        }
    } else {
        qCWarning(lcClient) << "Failed to parse authentication response";
    }
}

void ConnectionManager::handleAuthChallenge(const QByteArray& data) {
    AuthChallenge ch{};
    if ( ch.decode(data) ) {
        QByteArray salt = QByteArray::fromHex(ch.saltHex.toUtf8());
        if ( !salt.isEmpty() ) {
            // PBKDF2 派生: password + username + salt
            QByteArray input = (m_password + m_username).toUtf8();
            QByteArray derived = QPasswordDigestor::deriveKeyPbkdf2(
                QCryptographicHash::Sha256, input, salt,
                int(ch.iterations), quint64(ch.keyLength));

            AuthenticationRequest ar{};
            ar.username = m_username.isEmpty() ? QStringLiteral("guest") : m_username;
            ar.passwordHash = QString::fromLatin1(derived.toHex());
            m_tcpClient->sendMessage(MessageType::AUTHENTICATION_REQUEST, ar);
        }
    }
}

void ConnectionManager::sendHandshakeRequest() {
    HandshakeRequest request{};
    request.clientVersion = ProtocolConstants::ProtocolVersion;
    request.clientName = QStringLiteral("UltraDesktop Client");
    request.clientOS = getClientOS();
    m_tcpClient->sendMessage(MessageType::HANDSHAKE_REQUEST, request);
}

void ConnectionManager::sendSessionCapabilities() {
    SessionCapabilities caps{};
    caps.imageQuality = static_cast<quint8>(m_imageQuality);
    caps.colorDepth = static_cast<quint8>(m_colorDepth);
    m_tcpClient->sendMessage(MessageType::SESSION_CAPABILITIES, caps);
    qCDebug(lcClient) << "Sent session capabilities: quality" << m_imageQuality
                      << "colorDepth" << m_colorDepth;
}

void ConnectionManager::updateCredentials(const QString& username, const QString& password) {
    m_username = username;
    m_password = password;
}

void ConnectionManager::retryAuthentication() {
    if (m_connectionState != AuthFailed && m_connectionState != Disconnected) {
        qCWarning(lcClient) << "retryAuthentication: invalid state" << m_connectionState;
        return;
    }
    qCInfo(lcClient) << "重试认证，用户名:" << m_username;
    m_currentReconnectAttempts = 0;
    // 由上层（ConnectionLifecycle）先调用 updateCredentials 再调用此方法
    // 直接走 connectToHost 流程发起新连接
    connectToHost(m_host, m_port);
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
