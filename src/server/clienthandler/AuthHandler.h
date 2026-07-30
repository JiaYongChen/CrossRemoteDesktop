#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include <QtCore/QString>

/// 认证配置状态 — 显式区分「未配置」与「无密码」三态
///
/// 旧实现以空 digest 推断「无密码模式」。PBKDF2 改为异步派生注入后，
/// 空 digest 同时意味着「配置尚未到达」，authenticate() 会误判放行
/// （认证绕过竞态）。显式三态消除歧义：Unconfigured 状态必须拒绝认证。
enum class ConfigState {
    Unconfigured,   ///< 配置未到达：拒绝认证
    NoPassword,     ///< 无密码模式：直接通过
    Password        ///< 密码模式：需用户名 + 摘要比对
};

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

    /// 标记为无密码认证模式（配置就绪，authenticate 直接通过）
    void markNoPassword();

    /// 认证配置是否就绪（已标记无密码或已注入密码参数）
    bool isConfigured() const;

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

    /// 计算给定失败次数的指数退避延迟（isRateLimited 与认证失败延迟响应路径共用）
    /// 指数先钳位再乘方并封顶 AuthMaxDelayMs：任意 failCount 下安全（无溢出 UB）
    static int backoffDelayMs(int failCount);

    /// 获取 PBKDF2 参数（用于组装握手响应的认证参数）
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

    ConfigState m_state = ConfigState::Unconfigured;  ///< 认证配置状态（三态）
    QByteArray m_expectedSalt;
    QByteArray m_expectedDigest;
    QString m_expectedUsername;
    quint32 m_pbkdf2Iterations = 100000;
    quint32 m_pbkdf2KeyLength = 32;

    int m_failedAuthCount = 0;
    QDateTime m_lastFailedAuthTime;

    /// 失败登记公共逻辑：递增计数、刷新时间戳，返回锁定判定后的结果码（调用方持 m_mutex）
    int registerFailureLocked();
};
