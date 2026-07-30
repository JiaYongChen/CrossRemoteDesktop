#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include <QtCore/QString>

/// 认证处理器 — 从 ClientHandlerWorker 分离出的认证逻辑
///
/// 负责密码摘要注入、PBKDF2 参数管理、认证请求验证、
/// 速率限制（指数退避）和失败计数。
class AuthHandler {
public:
    AuthHandler();

    /// 设置期望的密码摘要和盐值
    void setExpectedPasswordDigest(const QByteArray& salt, const QByteArray& digest);

    /// 设置期望的用户名（用于比对）
    void setExpectedUsername(const QString& username);

    /// 设置 PBKDF2 参数
    void setPbkdf2Params(quint32 iterations, quint32 keyLength);

    /// 检查是否处于速率限制回退期
    bool isRateLimited() const;

    /// 验证认证请求
    /// @return 0=SUCCESS, 1=INVALID_USERNAME, 2=INVALID_PASSWORD, 3=ACCESS_DENIED
    /// @param username 客户端发送的用户名
    /// @param passwordHash 客户端发送的密码哈希(hex字符串)
    /// @return 认证结果 (AuthResult 枚举值)
    int authenticate(const QString& username, const QString& passwordHash);

    /// 登记一次无效认证请求（如解码失败）：递增失败计数并刷新时间戳用于速率限制。
    /// 与 authenticate() 失败路径一致，使畸形请求同样受指数退避约束。
    void registerInvalidAttempt();

    /// 获取期望的用户名（线程安全快照）
    QString expectedUsername() const;

    /// 获取当前失败次数
    int failedAuthCount() const;

    /// 获取 PBKDF2 参数（用于发送 AuthChallenge）
    quint32 pbkdf2Iterations() const;
    quint32 pbkdf2KeyLength() const;

    /// 获取期望的摘要（线程安全快照）
    QByteArray expectedDigest() const;

    /// 获取盐值（线程安全）
    QByteArray salt() const;

    /// 是否有密码保护
    bool hasPassword() const;

private:
    mutable QMutex m_mutex;

    QByteArray m_expectedSalt;
    QByteArray m_expectedDigest;
    QString m_expectedUsername;
    quint32 m_pbkdf2Iterations = 100000;
    quint32 m_pbkdf2KeyLength = 32;

    int m_failedAuthCount = 0;
    QDateTime m_lastFailedAuthTime;
};
