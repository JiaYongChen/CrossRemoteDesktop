#include "AuthHandler.h"
#include "../../common/logging/LoggingCategories.h"
#include <QtCore/QMutexLocker>
#include <QtCore/QDateTime>
#include <algorithm>

AuthHandler::AuthHandler() {}

void AuthHandler::setExpectedPasswordDigest(const QByteArray& salt, const QByteArray& digest) {
    QMutexLocker locker(&m_mutex);
    m_expectedSalt = salt;
    m_expectedDigest = digest;
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

int AuthHandler::authenticate(const QString& username, const QString& passwordHash, quint32 authMethod) {
    Q_UNUSED(username)

    QMutexLocker locker(&m_mutex);

    // 无密码保护 → 直接成功
    if (m_expectedDigest.isEmpty()) {
        return 0; // AuthResult::SUCCESS
    }

    if (authMethod != 1) {
        qCWarning(lcServerClientHandler) << "不支持的认证方法:" << authMethod;
        return 2; // AuthResult::INVALID_PASSWORD
    }

    if (passwordHash.isEmpty()) {
        return -1; // 需要挑战（sendAuthChallenge）
    }

    QByteArray clientDigest = QByteArray::fromHex(passwordHash.toUtf8());
    if (clientDigest == m_expectedDigest) {
        return 0; // AuthResult::SUCCESS
    }

    m_failedAuthCount++;
    m_lastFailedAuthTime = QDateTime::currentDateTime();
    qCWarning(lcServerClientHandler) << "客户端认证失败 (失败次数:" << m_failedAuthCount << "/" << SecurityConstants::MaxAuthFailures << ")";

    if (m_failedAuthCount >= SecurityConstants::MaxAuthFailures) {
        return 3; // ACCESS_DENIED → 断开连接
    }

    return 2; // INVALID_PASSWORD (含回退延迟)
}

void AuthHandler::markAuthenticated() {
    // No mutex needed here — called only after successful auth in the same thread
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
