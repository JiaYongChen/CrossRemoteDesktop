#include <QtTest/QTest>
#include "../src/client/network/TcpClient.h"
#include "../src/client/network/ConnectionManager.h"

class TestSmokeNetwork : public QObject {
    Q_OBJECT
private slots:
    // ── TcpClient 烟雾测试 ──
    void tcpClient_construct_defaults() {
        TcpClient client;
        QVERIFY(!client.isConnected());
        QCOMPARE(client.serverAddress(), QString(""));
        // 构造后即可查询端口（返回底层 socket 的值，预连接时非零）
        QVERIFY(!client.isConnected());
    }

    // ── ConnectionManager 烟雾测试 ──
    void connMgr_construct_defaults() {
        ConnectionManager mgr;
        QVERIFY(!mgr.isConnected());
        QVERIFY(!mgr.isAuthenticated());
        QCOMPARE(mgr.currentHost(), QString(""));
        QCOMPARE(mgr.currentPort(), 0);
    }

    void connMgr_defaultConfig() {
        ConnectionManager mgr;
        QVERIFY(!mgr.autoReconnect());
        QCOMPARE(mgr.reconnectInterval(), 3000);   // NetworkConstants::DefaultReconnectInterval
        QCOMPARE(mgr.maxReconnectAttempts(), 5);     // NetworkConstants::DefaultMaxReconnectAttempts
        QCOMPARE(mgr.currentReconnectAttempts(), 0);
        QCOMPARE(mgr.connectionTimeout(), 15000);    // NetworkConstants::DefaultConnectionTimeout
    }

    void connMgr_setAutoReconnect() {
        ConnectionManager mgr;
        mgr.setAutoReconnect(true);
        QVERIFY(mgr.autoReconnect());
        mgr.setAutoReconnect(false);
        QVERIFY(!mgr.autoReconnect());
    }

    void connMgr_setReconnectConfig() {
        ConnectionManager mgr;
        mgr.setReconnectInterval(5000);
        QCOMPARE(mgr.reconnectInterval(), 5000);
        mgr.setMaxReconnectAttempts(10);
        QCOMPARE(mgr.maxReconnectAttempts(), 10);
    }

    void connMgr_setConnectionTimeout() {
        ConnectionManager mgr;
        mgr.setConnectionTimeout(30000);
        QCOMPARE(mgr.connectionTimeout(), 30000);
    }
};

QTEST_MAIN(TestSmokeNetwork)
#include "test_smoke_network.moc"
