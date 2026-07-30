#include <QtTest/QTest>
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
};

QTEST_MAIN(TestAuthHandler)
#include "test_authhandler.moc"
