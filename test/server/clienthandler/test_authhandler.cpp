#include <QtTest/QTest>
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
        // 用户名不符 → INVALID_USERNAME 且不计数
        QCOMPARE(handler.authenticate(QStringLiteral("root"), QStringLiteral("deadbeef")),
                 static_cast<int>(AuthResult::INVALID_USERNAME));
        QCOMPARE(handler.failedAuthCount(), 0);
        // 摘要不符 → INVALID_PASSWORD 且计数
        QCOMPARE(handler.authenticate(QStringLiteral("admin"), QStringLiteral("0000")),
                 static_cast<int>(AuthResult::INVALID_PASSWORD));
        QCOMPARE(handler.failedAuthCount(), 1);
    }
};

QTEST_MAIN(TestAuthHandler)
#include "test_authhandler.moc"
