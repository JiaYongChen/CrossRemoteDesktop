#include "server/clienthandler/AuthHandler.h"

#include <algorithm>

#include <QtCore/QDateTime>
#include <QtCore/QMutexLocker>

#include "common/config/SecurityConstants.h"
#include "common/error/RdError.h"
#include "common/network/Protocol.h"
#include "common/logging/LoggingCategories.h"

AuthHandler::AuthHandler() {}

void AuthHandler::setExpectedPasswordDigest(const QByteArray& salt, const QByteArray& digest) {
    QMutexLocker locker(&m_mutex);
    m_expectedSalt = salt;
    m_expectedDigest = digest;
    m_state = ConfigState::Password;
}

void AuthHandler::setExpectedUsername(const QString& username) {
    QMutexLocker locker(&m_mutex);
    m_expectedUsername = username.trimmed();
}

void AuthHandler::setPbkdf2Params(quint32 iterations, quint32 keyLength) {
    QMutexLocker locker(&m_mutex);
    m_pbkdf2Iterations = iterations;
    m_pbkdf2KeyLength = keyLength;
}

void AuthHandler::markNoPassword() {
    QMutexLocker locker(&m_mutex);
    m_state = ConfigState::NoPassword;
}

bool AuthHandler::isConfigured() const {
    QMutexLocker locker(&m_mutex);
    return m_state != ConfigState::Unconfigured;
}

bool AuthHandler::isRateLimited() const {
    QMutexLocker locker(&m_mutex);
    if (m_failedAuthCount <= 0 || !m_lastFailedAuthTime.isValid()) return false;

    const qint64 elapsedMs = m_lastFailedAuthTime.msecsTo(QDateTime::currentDateTime());
    return elapsedMs < backoffDelayMs(m_failedAuthCount);
}

int AuthHandler::authenticate(const QString& username, const QString& passwordHash) {
    QMutexLocker locker(&m_mutex);

    // 未配置即拒绝（纵深防御：worker 层 isConfigured() 门控应已先行断连，
    // 到达此处说明协议违规或内部时序错误）——不得以「空 digest」误判为「无密码」放行
    if (m_state == ConfigState::Unconfigured) {
        qCCritical(lcServerClientHandler) << "认证配置未就绪即收到认证请求，拒绝认证";
        return static_cast<int>(AuthResult::INVALID_PASSWORD);
    }

    // 无密码保护模式 → 直接成功
    if (m_state == ConfigState::NoPassword) {
        return static_cast<int>(AuthResult::SUCCESS);
    }

    // 密码模式：任何失败对外不可区分——统一返回通用 INVALID_PASSWORD（不再回
    // INVALID_USERNAME 独立码，消除用户名枚举 oracle），真实原因仅入服务端日志。
    // 全部失败计入计数，使 MaxAuthFailures 阶梯锁定可达（连接内重试模型）。

    // 用户名校验（trim 避免尾随空格导致误判）
    if (!m_expectedUsername.isEmpty()
        && username.trimmed() != m_expectedUsername.trimmed()) {
        qCWarning(lcServerClientHandler) << "认证失败：用户名无效"
                                         << "期望:" << m_expectedUsername
                                         << "收到:" << username;
        return registerFailureLocked();
    }

    // 密码哈希为空 → 拒绝并计数
    if (passwordHash.isEmpty()) {
        qCWarning(lcServerClientHandler) << "认证失败：客户端未提供密码哈希";
        return registerFailureLocked();
    }

    // 比对摘要
    QByteArray clientDigest = QByteArray::fromHex(passwordHash.toUtf8());
    if (clientDigest == m_expectedDigest) {
        return static_cast<int>(AuthResult::SUCCESS);
    }

    qCWarning(lcServerClientHandler) << "认证失败：密码摘要不符";
    return registerFailureLocked();
}

int AuthHandler::registerFailureLocked() {
    m_failedAuthCount++;
    m_lastFailedAuthTime = QDateTime::currentDateTime();
    qCWarning(lcServerClientHandler) << "客户端认证失败 (失败次数:" << m_failedAuthCount
                                     << "/" << SecurityConstants::MaxAuthFailures << ")";

    if (m_failedAuthCount >= SecurityConstants::MaxAuthFailures) {
        return static_cast<int>(AuthResult::ACCESS_DENIED);
    }
    return static_cast<int>(AuthResult::INVALID_PASSWORD);
}

int AuthHandler::backoffDelayMs(int failCount) {
    if (failCount <= 0) {
        return SecurityConstants::AuthBaseDelayMs;
    }
    // 先钳位指数再移位相乘：failCount 超大时封顶于 AuthMaxDelayMs，行为与旧式
    // 「先乘方后 min」等价，但消除钳位前求值的 int 溢出/移位 UB（旧式 n≥23 溢出）
    const int exponent = std::min(failCount - 1, 5);   // 1000*2^5=32000 ≥ AuthMaxDelayMs
    return std::min(SecurityConstants::AuthBaseDelayMs * (1 << exponent),
                    SecurityConstants::AuthMaxDelayMs);
}

void AuthHandler::registerInvalidAttempt() {
    QMutexLocker locker(&m_mutex);
    m_failedAuthCount++;
    m_lastFailedAuthTime = QDateTime::currentDateTime();
    qCWarning(lcServerClientHandler) << "登记无效认证请求 (失败次数:" << m_failedAuthCount
                                     << "/" << SecurityConstants::MaxAuthFailures << ")";
}

int AuthHandler::failedAuthCount() const {
    QMutexLocker locker(&m_mutex);
    return m_failedAuthCount;
}

quint32 AuthHandler::pbkdf2Iterations() const {
    QMutexLocker locker(&m_mutex);
    return m_pbkdf2Iterations;
}

quint32 AuthHandler::pbkdf2KeyLength() const {
    QMutexLocker locker(&m_mutex);
    return m_pbkdf2KeyLength;
}

QByteArray AuthHandler::expectedDigest() const {
    QMutexLocker locker(&m_mutex);
    return m_expectedDigest;
}

QByteArray AuthHandler::salt() const {
    QMutexLocker locker(&m_mutex);
    return m_expectedSalt;
}

bool AuthHandler::hasPassword() const {
    QMutexLocker locker(&m_mutex);
    return m_state == ConfigState::Password;
}

QString AuthHandler::expectedUsername() const {
    QMutexLocker locker(&m_mutex);
    return m_expectedUsername;
}
