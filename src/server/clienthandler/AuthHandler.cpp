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

bool AuthHandler::isRateLimited() const {
    QMutexLocker locker(&m_mutex);
    if (m_failedAuthCount <= 0 || !m_lastFailedAuthTime.isValid()) return false;

    int requiredDelayMs = std::min(
        SecurityConstants::AuthBaseDelayMs * (1 << (m_failedAuthCount - 1)),
        SecurityConstants::AuthMaxDelayMs);
    qint64 elapsedMs = m_lastFailedAuthTime.msecsTo(QDateTime::currentDateTime());
    return elapsedMs < requiredDelayMs;
}

int AuthHandler::authenticate(const QString& username, const QString& passwordHash) {
    QMutexLocker locker(&m_mutex);

    // 无密码保护 → 直接成功
    if (m_expectedDigest.isEmpty()) {
        return static_cast<int>(AuthResult::SUCCESS);
    }

    // 先校验用户名（trim 避免尾随空格导致误判）
    if (!m_expectedUsername.isEmpty()
        && username.trimmed() != m_expectedUsername.trimmed()) {
        qCWarning(lcServerClientHandler) << "认证失败：用户名无效"
                                         << "期望:" << m_expectedUsername
                                         << "收到:" << username;
        return static_cast<int>(AuthResult::INVALID_USERNAME);
    }

    // 密码哈希为空 → 拒绝（不再返回 -1 触发挑战，服务端现在主动下发）
    if (passwordHash.isEmpty()) {
        qCWarning(lcServerClientHandler) << "认证失败：客户端未提供密码哈希";
        return static_cast<int>(AuthResult::INVALID_PASSWORD);
    }

    // 比对摘要
    QByteArray clientDigest = QByteArray::fromHex(passwordHash.toUtf8());
    if (clientDigest == m_expectedDigest) {
        return static_cast<int>(AuthResult::SUCCESS);
    }

    // 认证失败：递增计数并速率限制
    m_failedAuthCount++;
    m_lastFailedAuthTime = QDateTime::currentDateTime();
    qCWarning(lcServerClientHandler) << "客户端认证失败 (失败次数:" << m_failedAuthCount
                                     << "/" << SecurityConstants::MaxAuthFailures << ")";

    if (m_failedAuthCount >= SecurityConstants::MaxAuthFailures) {
        return static_cast<int>(AuthResult::ACCESS_DENIED);
    }

    return static_cast<int>(AuthResult::INVALID_PASSWORD);
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
    return !m_expectedDigest.isEmpty();
}

QString AuthHandler::expectedUsername() const {
    QMutexLocker locker(&m_mutex);
    return m_expectedUsername;
}
