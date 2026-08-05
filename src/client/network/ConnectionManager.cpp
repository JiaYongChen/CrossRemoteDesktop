#include "ConnectionManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QTimer>
#include <QtNetwork/QPasswordDigestor>

#include "client/network/ServerTrustStore.h"
#include "client/network/TcpClient.h"
#include "common/config/NetworkConstants.h"
#include "common/config/SecurityConstants.h"
#include "common/config/SettingsManager.h"
#include "common/error/RdError.h"
#include "common/logging/LoggingCategories.h"
#include "common/network/AppVersion.h"

ConnectionManager::ConnectionManager(QObject* parent, SettingsManager* settings)
    : QObject(parent)
    , m_tcpClient(nullptr)
    , m_currentPort(0)
    , m_connectionTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_autoReconnect(false)
    , m_reconnectInterval(NetworkConstants::DefaultReconnectInterval)
    , m_maxReconnectAttempts(NetworkConstants::DefaultMaxReconnectAttempts)
    , m_currentReconnectAttempts(0)
    , m_connectionTimeout(NetworkConstants::DefaultConnectionTimeout) {
    if ( settings ) {
        m_trustStore = std::make_unique<ServerTrustStore>(*settings);
    }
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
    // 显式连接请求是新的用户意图：复位残留标记，若存在活动连接则硬断旧连接
    m_userInitiatedDisconnect = false;
    if ( m_tcpClient && (m_tcpClient->isConnected() || m_connectionTimer->isActive()) ) {
        disconnectFromHost();         // 停定时器 + 置用户主动标记
        m_tcpClient->abort();         // abort 仅 Connected 状态同步触发 onTcpDisconnected；
        m_userInitiatedDisconnect = false;  // Connecting 状态下标记未被消费，显式清除
    }

    // 注入已保存的受信证书：TcpClient 以其为 CA 执行 VerifyPeer 验证；
    // 无记录（首次连接）不注入 → VerifyNone，由 TOFU 流程记录后经重连走验证路径
    if ( m_trustStore ) {
        const QString endpoint = ServerTrustStore::endpointFor(host, static_cast<quint16>(port));
        if ( const auto cert = m_trustStore->storedCertificate(endpoint) ) {
            m_tcpClient->setTrustedCertificate(*cert);
        } else {
            m_tcpClient->clearTrustedCertificate();   // 无记录则清除残留证书——防旧 endpoint 的 CA 残留到新连接
        }
    }

    // 新连接开始前复位认证状态（认证参数每连接由服务端重新下发）
    m_authenticated = false;

    emit connecting();
    startConnection(host, port);
}

void ConnectionManager::disconnectFromHost() {
    stopAutoReconnect();
    m_reconnectArmed = false;

    // 无活动连接（已断开或从未连接）→ 幂等返回，无 disconnected 信号会来
    if ( !m_tcpClient || (!m_tcpClient->isConnected() && !m_connectionTimer->isActive()) ) {
        return;
    }

    m_currentReconnectAttempts = 0;
    m_connectionTimer->stop();
    m_userInitiatedDisconnect = true;

    m_tcpClient->disconnectFromHost();
}

void ConnectionManager::startConnection(const QString& host, int port) {
    m_currentHost = host;
    m_currentPort = port;

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
    return m_authenticated;
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
        m_reconnectArmed = false;
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
// 自动重连 — 单一决策点（调用方不预置状态，以返回值驱动）
// ═══════════════════════════════════════════════════════════════════

bool ConnectionManager::startAutoReconnect() {
    if ( !m_autoReconnect || m_currentReconnectAttempts >= m_maxReconnectAttempts ) {
        m_reconnectArmed = false;
        return false;
    }

    // 已有待触发定时器：不重复计数（error+disconnected 双路径去重）
    if ( m_reconnectTimer->isActive() ) {
        return true;
    }

    m_currentReconnectAttempts++;
    m_reconnectTimer->setInterval(m_reconnectInterval);
    m_reconnectTimer->start();
    m_reconnectArmed = true;
    emit reconnecting();
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
    m_reconnectArmed = false;
    emit connected();

    // TOFU 首连证书记录：VerifyNone 成功时 TcpClient 持有对端证书，
    // 但尚未注入 CA → 记录后下次经 VerifyPeer 自动验证
    if ( m_trustStore && m_tcpClient ) {
        const QSslCertificate peerCert = m_tcpClient->peerCertificate();
        if ( !peerCert.isNull() && !m_trustStore->storedCertificate(
                 ServerTrustStore::endpointFor(m_currentHost, static_cast<quint16>(m_currentPort)) ) ) {
            m_trustStore->recordTrust(
                ServerTrustStore::endpointFor(m_currentHost, static_cast<quint16>(m_currentPort)),
                peerCert);
            qCInfo(lcClient) << "TOFU: 首次连接，已自动记录服务端证书";
        }
    }

    sendVersionExchange();
}

void ConnectionManager::onTcpDisconnected() {
    m_connectionTimer->stop();
    m_authenticated = false;

    // ── 分支 1：用户主动断开 ──
    if ( m_userInitiatedDisconnect ) {
        m_userInitiatedDisconnect = false;
        m_currentReconnectAttempts = 0;
        cleanupConnection();
        emit disconnected();
        return;
    }

    // ── 分支 2：意外断线 — startAutoReconnect 单一决策点 ──
    // 调用方不预置状态：startAutoReconnect 成功返回则重连定时器已武装（reconnecting 已发射），
    // 失败（!autoReconnect || 预算耗尽）则收敛 disconnected。
    // 若定时器已活跃（前置错误处理已武装），返回 true 且只修正状态不重复计数。
    if ( !startAutoReconnect() ) {
        cleanupConnection();
        emit disconnected();
    }
    // 成功路径：重连定时器已武装，等待 onReconnectTimer 发起新连接
}

void ConnectionManager::onTcpError(const RdError& error) {
    if ( m_userInitiatedDisconnect ) {
        m_connectionTimer->stop();
        return;
    }

    m_connectionTimer->stop();
    emit errorOccurred(error);

    // 确保 socket 回到 UnconnectedState（SocketTimeoutError 等错误不自动转换）
    if ( m_tcpClient ) {
        m_tcpClient->abort();
    }
    // abort() 仅 Connected/Closing 态同步触发 onTcpDisconnected——
    // Connecting/Unconnected 态需直接调用 startAutoReconnect
    if ( !startAutoReconnect() ) {
        m_authenticated = false;
        cleanupConnection();
        emit disconnected();
    }
}

void ConnectionManager::onConnectionTimeout() {
    qCWarning(lcClient) << "ConnectionManager: Connection timeout";

    if ( m_userInitiatedDisconnect ) {
        m_connectionTimer->stop();
        return;
    }

    m_connectionTimer->stop();
    emit errorOccurred(RdError(ErrorCode::NetworkConnectionFailed,
        QStringLiteral("连接超时"), "ConnectionManager"));

    if ( m_tcpClient ) {
        m_tcpClient->abort();
    }
    // abort() 仅 Connected/Closing 态同步触发 onTcpDisconnected——
    // Connecting/Unconnected 态需直接调用 startAutoReconnect
    if ( !startAutoReconnect() ) {
        m_authenticated = false;
        cleanupConnection();
        emit disconnected();
    }
}

void ConnectionManager::onReconnectTimer() {
    if ( m_currentHost.isEmpty() || m_currentPort <= 0 ) {
        return;
    }
    startConnection(m_currentHost, m_currentPort);
}

// ═══════════════════════════════════════════════════════════════════
// 内部工具
// ═══════════════════════════════════════════════════════════════════

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
    m_reconnectArmed = false;
    m_currentReconnectAttempts = 0;
    m_currentHost.clear();
    m_currentPort = 0;
}

// ═══════════════════════════════════════════════════════════════════
// 消息处理
// ═══════════════════════════════════════════════════════════════════

bool ConnectionManager::mayProcessServerMessages() const {
    // 管道就绪判定：TCP+TLS 已建立即可处理 RDCP 消息（PKI 化后不存在
    // 「管道通了但身份未验证」的挂起中间态——验证失败即断连）
    return m_tcpClient && m_tcpClient->isConnected();
}

void ConnectionManager::onTcpMessageReceived(MessageType type, const QByteArray& payload) {
    // 管道就绪守卫：仅在 TCP+TLS 已建立时处理 RDCP 消息
    if ( !mayProcessServerMessages() ) {
        qCDebug(lcClient) << "onTcpMessageReceived: 忽略消息（管道未就绪），type:" << static_cast<int>(type);
        return;
    }

    // 入站剪贴板仅认证后放行：认证前服务端虽已验证但用户尚未授权，
    // 授权前写入本机系统剪贴板属内容注入（合法服务端不会在认证前发送剪贴板）
    if ( type == MessageType::CLIPBOARD_DATA && !m_authenticated ) {
        qCDebug(lcClient) << "onTcpMessageReceived: 忽略 CLIPBOARD_DATA（未认证态）";
        return;
    }

    switch ( type ) {
        case MessageType::VERSION_EXCHANGE_RESPONSE:
            handleVersionExchangeResponse(payload);
            break;
        case MessageType::AUTHENTICATION_RESPONSE:
            handleAuthenticationResponse(payload);
            break;
        default:
            emit messageReceived(type, payload);
            break;
    }
}

void ConnectionManager::handleVersionExchangeResponse(const QByteArray& data) {
    VersionExchangeResponse response;
    if ( !response.decode(data) ) {
        const RdError error(ErrorCode::DecodeFailed,
                            tr("版本交换响应解码失败"), "ConnectionManager");
        qCWarning(lcClient) << error.logLabel();
        emit errorOccurred(error);
        disconnectFromHost();
        return;
    }

    qCDebug(lcClient) << "Received version exchange response from server";

    // 版本闸门：应用版本完全相等校验。不匹配/畸形版本直接断连——
    // 不得继续处理认证参数，防止向不兼容对端发送 PBKDF2 密码证明
    if ( !appVersionMatches(response.appVersion) ) {
        const QString serverVer = response.appVersion;
        const QString localVer = QCoreApplication::applicationVersion();
        qCWarning(lcClient) << "版本不兼容: server" << serverVer << "local" << localVer;
        emit versionMismatched(serverVer, localVer);
        disconnectFromHost();
        return;
    }

    if ( response.saltHex.isEmpty() ) {
        // 无密码模式：等待服务端直通 AUTHENTICATION_RESPONSE，不主动发送认证请求
        return;
    }

    // 密码模式：认证参数随版本交换响应下发（协议 v3）。参数上限校验防恶意服务端
    // 下发超大值致客户端资源耗尽（认证前单包 DoS），通过后内联派生并发送认证请求
    QByteArray salt = QByteArray::fromHex(response.saltHex.toUtf8());
    if ( salt.isEmpty() || response.iterations == 0
         || response.iterations > SecurityConstants::MaxPbkdf2Iterations
         || response.keyLength == 0
         || response.keyLength > SecurityConstants::MaxPbkdf2KeyLength ) {
        const RdError error(ErrorCode::DecodeFailed,
                            tr("版本交换响应认证参数越界"), "ConnectionManager");
        qCWarning(lcClient) << error.logLabel()
                            << "iterations:" << response.iterations << "keyLength:" << response.keyLength;
        emit errorOccurred(error);
        disconnectFromHost();
        return;
    }

    // 认证参数仅用于本次派生，不缓存（每连接由服务端重新下发）
    sendAuthenticationRequest(salt, response.iterations, response.keyLength);
}

void ConnectionManager::handleAuthenticationResponse(const QByteArray& data) {
    AuthenticationResponse response;
    if ( !response.decode(data) ) {
        const RdError error(ErrorCode::DecodeFailed, tr("认证响应解码失败"), "ConnectionManager");
        qCWarning(lcClient) << error.logLabel();
        emit errorOccurred(error);
        disconnectFromHost();
        return;
    }

    if ( response.result == AuthResult::SUCCESS ) {
        qCInfo(lcClient) << "Authentication successful, session ID:" << response.sessionId;
        m_connectionTimer->stop();
        stopAutoReconnect();
        m_currentReconnectAttempts = 0;
        m_authenticated = true;
        emit authenticated();
        sendEncodePrefs();   // 认证成功后告知服务端编码偏好
        return;
    }

    // 认证被拒是预期结果而非系统故障——以独立信号承载，不进入 errorOccurred
    QString reason;
    switch ( response.result ) {
        case AuthResult::INVALID_CREDENTIALS:
            reason = tr("认证失败：用户名或密码错误");
            break;
        case AuthResult::ACCESS_DENIED:
            reason = tr("尝试次数过多，账户已被暂时锁定，请稍后重试。");
            break;
        default:
            reason = tr("认证失败");
            break;
    }
    qCWarning(lcClient) << "Authentication rejected, result:" << static_cast<int>(response.result);
    emit authenticationFailed(response.result, reason);
    disconnectFromHost();
}

void ConnectionManager::sendAuthenticationRequest(const QByteArray& salt, quint32 iterations, quint32 keyLength) {
    // PBKDF2 派生: password + username + salt（参数由服务端本次下发）
    QByteArray input = (m_password + m_username).toUtf8();
    QByteArray derived = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256, input, salt,
        int(iterations), quint64(keyLength));

    AuthenticationRequest ar{};
    ar.username = m_username.isEmpty() ? QStringLiteral("guest") : m_username;
    ar.passwordHash = QString::fromLatin1(derived.toHex());
    m_tcpClient->sendMessage(MessageType::AUTHENTICATION_REQUEST, ar);
}

void ConnectionManager::sendVersionExchange() {
    VersionExchange request{};
    request.appVersion = QCoreApplication::applicationVersion();
    request.clientName = QStringLiteral("UltraDesktop Client");
    request.clientOS = getClientOS();
    m_tcpClient->sendMessage(MessageType::VERSION_EXCHANGE, request);
}

void ConnectionManager::sendEncodePrefs() {
    EncodePrefs prefs{};
    prefs.imageQuality = static_cast<quint8>(m_imageQuality);
    prefs.colorDepth = static_cast<quint8>(m_colorDepth);
    m_tcpClient->sendMessage(MessageType::ENCODE_PREFS, prefs);
    qCDebug(lcClient) << "Sent encode prefs: quality" << m_imageQuality
                      << "colorDepth" << m_colorDepth;
}

void ConnectionManager::updateCredentials(const QString& username, const QString& password) {
    m_username = username;
    m_password = password;
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
