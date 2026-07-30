#include <QtTest/QTest>
#include "common/config/SecurityConstants.h"
#include "common/network/Protocol.h"
#include "server/clienthandler/AuthHandler.h"

class TestAuthHandler : public QObject {
    Q_OBJECT
private slots:
    // ── registerInvalidAttempt：畸形认证请求登记为一次失败 ──
    void registerInvalidAttempt_incrementsCount() {
        AuthHandler handler;
        QCOMPARE(handler.failedAuthCount(), 0);
        handler.registerInvalidAttempt();
        QCOMPARE(handler.failedAuthCount(), 1);
        handler.registerInvalidAttempt();
        QCOMPARE(handler.failedAuthCount(), 2);
    }

    void registerInvalidAttempt_triggersRateLimit() {
        AuthHandler handler;
        QVERIFY(!handler.isRateLimited());   // 初始无失败，不受限
        handler.registerInvalidAttempt();
        // 刚登记失败即处于退避窗口内（elapsed≈0 < AuthBaseDelayMs=1000）
        QVERIFY(handler.isRateLimited());
    }

    // ── 回归：认证配置三态语义（审查发现 1/2：认证绕过竞态）──
    // 根因：旧实现以 m_expectedDigest.isEmpty() 同时表达「无密码模式」与
    // 「PBKDF2 异步派生尚未注入」，后者被误判为无密码 → 直接 SUCCESS。

    // 配置未就绪时，任何凭据都不得通过认证
    void unconfigured_rejectsAnyCredentials() {
        AuthHandler handler;
        const int result = handler.authenticate(QStringLiteral("anyone"), QStringLiteral("deadbeef"));
        QVERIFY2(result != static_cast<int>(AuthResult::SUCCESS),
                 "未配置的 AuthHandler 不得返回 SUCCESS（空 digest 不等于无密码模式）");
    }

    // 配置未就绪时，空哈希同样不得通过
    void unconfigured_rejectsEmptyHash() {
        AuthHandler handler;
        const int result = handler.authenticate(QStringLiteral("anyone"), QString());
        QVERIFY2(result != static_cast<int>(AuthResult::SUCCESS),
                 "未配置的 AuthHandler 不得对空哈希返回 SUCCESS");
    }

    // 无密码模式须经显式标记后才直通（不再依赖空 digest 推断）
    void noPassword_succeedsAfterExplicitMark() {
        AuthHandler handler;
        handler.markNoPassword();
        QVERIFY(handler.isConfigured());
        QVERIFY(!handler.hasPassword());
        QCOMPARE(handler.authenticate(QStringLiteral("anyone"), QString()),
                 static_cast<int>(AuthResult::SUCCESS));
    }

    // 密码模式：三态改造不改变正常的用户名/摘要比对路径
    void password_normalComparisonPath() {
        AuthHandler handler;
        handler.setExpectedUsername(QStringLiteral("admin"));
        handler.setExpectedPasswordDigest(QByteArray::fromHex("0011223344"),
                                          QByteArray::fromHex("deadbeef"));
        QVERIFY(handler.isConfigured());
        QVERIFY(handler.hasPassword());

        // 正确用户名 + 正确摘要 → SUCCESS
        QCOMPARE(handler.authenticate(QStringLiteral("admin"), QStringLiteral("deadbeef")),
                 static_cast<int>(AuthResult::SUCCESS));
        // 用户名不符 → 通用 INVALID_PASSWORD（失败统一化，无独立响应码）且计数
        QCOMPARE(handler.authenticate(QStringLiteral("root"), QStringLiteral("deadbeef")),
                 static_cast<int>(AuthResult::INVALID_PASSWORD));
        QCOMPARE(handler.failedAuthCount(), 1);
        // 摘要不符 → 同样通用 INVALID_PASSWORD 且计数
        QCOMPARE(handler.authenticate(QStringLiteral("admin"), QStringLiteral("0000")),
                 static_cast<int>(AuthResult::INVALID_PASSWORD));
        QCOMPARE(handler.failedAuthCount(), 2);
    }

    // ── 连接内重试模型（2026-07-30 设计变更）：失败统一化 + 全失败计数 + 阶梯锁定 ──
    // 任何可恢复失败对外表现一致（通用 INVALID_PASSWORD：消除用户名枚举 oracle），
    // 且全部计入失败计数，使 MaxAuthFailures 阶梯锁定真正可达。

    // 用户名错误、空哈希与密码错误：同码、同计数
    void failure_unifiedCodeAndCounted() {
        AuthHandler handler;
        handler.setExpectedUsername(QStringLiteral("admin"));
        handler.setExpectedPasswordDigest(QByteArray::fromHex("0011"), QByteArray::fromHex("deadbeef"));

        // 用户名不符 → 通用码 + 计数（旧行为：INVALID_USERNAME 且不计数）
        QCOMPARE(handler.authenticate(QStringLiteral("root"), QStringLiteral("deadbeef")),
                 static_cast<int>(AuthResult::INVALID_PASSWORD));
        QCOMPARE(handler.failedAuthCount(), 1);

        // 空哈希 → 通用码 + 计数（旧行为：不计数，使限速恒不触发）
        QCOMPARE(handler.authenticate(QStringLiteral("admin"), QString()),
                 static_cast<int>(AuthResult::INVALID_PASSWORD));
        QCOMPARE(handler.failedAuthCount(), 2);
    }

    // 混合失败累积到上限 → ACCESS_DENIED 终局锁定（MaxAuthFailures=5）
    void failure_mixedLadderReachesLockout() {
        AuthHandler handler;
        handler.setExpectedUsername(QStringLiteral("admin"));
        handler.setExpectedPasswordDigest(QByteArray::fromHex("0011"), QByteArray::fromHex("deadbeef"));

        for (int i = 0; i < SecurityConstants::MaxAuthFailures; ++i) {
            const int expected = (i < SecurityConstants::MaxAuthFailures - 1)
                ? static_cast<int>(AuthResult::INVALID_PASSWORD)
                : static_cast<int>(AuthResult::ACCESS_DENIED);
            // 用户名与哈希皆错：第 1..4 次通用失败码，第 5 次锁定码
            QCOMPARE(handler.authenticate(QStringLiteral("root"), QStringLiteral("0000")), expected);
        }
    }

    // 退避曲线：单一实现（isRateLimited 与 worker 共用），指数预钳位消除 UB
    void backoffDelayMs_curve() {
        QCOMPARE(AuthHandler::backoffDelayMs(0), SecurityConstants::AuthBaseDelayMs);   // 1000
        QCOMPARE(AuthHandler::backoffDelayMs(1), SecurityConstants::AuthBaseDelayMs);   // 1000
        QCOMPARE(AuthHandler::backoffDelayMs(2), 2000);
        QCOMPARE(AuthHandler::backoffDelayMs(3), 4000);
        QCOMPARE(AuthHandler::backoffDelayMs(5), 16000);
        QCOMPARE(AuthHandler::backoffDelayMs(6), SecurityConstants::AuthMaxDelayMs);    // 32000 → 封顶 30000
        QCOMPARE(AuthHandler::backoffDelayMs(100), SecurityConstants::AuthMaxDelayMs);  // 大计数钳位无 UB
    }
};

QTEST_MAIN(TestAuthHandler)
#include "test_authhandler.moc"
