#include <QtTest/QTest>
#include "../../src/client/network/TcpClient.h"
#include "../../src/client/network/ConnectionManager.h"

class TestSmokeNetwork : public QObject {
    Q_OBJECT
private slots:
    // ── TcpClient 烟雾测试 ──
    void tcpClient_construct_defaults() {
        TcpClient client;
        QVERIFY(!client.isConnected());
    }

    // ── ConnectionManager 烟雾测试 ──
    void connMgr_construct_defaults() {
        ConnectionManager mgr;
        QVERIFY(!mgr.isConnected());
        QVERIFY(!mgr.isAuthenticated());
    }
};

QTEST_MAIN(TestSmokeNetwork)
#include "test_smoke_network.moc"
