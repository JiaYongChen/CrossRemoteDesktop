#include <QtTest/QtTest>
#include <QSignalSpy>
#include "../src/client/managers/connectionmanager.h"
#include "../src/common/core/logger.h"

class TestConnectionManager : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        // Enable concise console logging for tests
        Logger::instance()->setLogTargets(Logger::Console);
        Logger::instance()->setLogLevel(Logger::Debug);
    }

    void initial_state() {
        ConnectionManager cm;
        QCOMPARE(cm.connectionState(), ConnectionManager::Disconnected);
        QVERIFY(!cm.isConnected());
        QVERIFY(!cm.isAuthenticated());
        QCOMPARE(cm.currentReconnectAttempts(), 0);
    }

    void state_transitions_via_private_slots() {
        ConnectionManager cm;
        QSignalSpy stSpy(&cm, &ConnectionManager::connectionStateChanged);
        QSignalSpy cSpy(&cm, &ConnectionManager::connected);
        QSignalSpy aSpy(&cm, &ConnectionManager::authenticated);
        QSignalSpy dSpy(&cm, &ConnectionManager::disconnected);
        QSignalSpy eSpy(&cm, &ConnectionManager::errorOccurred);

        // Simulate TCP connected
        QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Qt::DirectConnection));
        QCOMPARE(cm.connectionState(), ConnectionManager::Connected);
        QVERIFY(cSpy.count() == 1);
        QVERIFY(stSpy.count() >= 1);

        // Simulate authenticated
        QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpAuthenticated", Qt::DirectConnection));
        QCOMPARE(cm.connectionState(), ConnectionManager::Authenticated);
        QVERIFY(aSpy.count() == 1);

        // Simulate disconnect
        QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpDisconnected", Qt::DirectConnection));
        QCOMPARE(cm.connectionState(), ConnectionManager::Disconnected);
        QVERIFY(dSpy.count() == 1);

        // Simulate error
        QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpError", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("boom"))));
        QCOMPARE(cm.connectionState(), ConnectionManager::Error);
        QVERIFY(eSpy.count() == 1);
    }

    void autoreconnect_counters_increment() {
        ConnectionManager cm;
        cm.setAutoReconnect(true);
        cm.setMaxReconnectAttempts(2);
        QCOMPARE(cm.currentReconnectAttempts(), 0);

        // Trigger an error which should start auto-reconnect flow
        QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpError", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("neterr"))));
        QCOMPARE(cm.connectionState(), ConnectionManager::Error);
        QCOMPARE(cm.currentReconnectAttempts(), 1);

        // Trigger again within error path until cap
        QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpDisconnected", Qt::DirectConnection));
        // onTcpDisconnected schedules reconnect if enabled; counter increments in startAutoReconnect
        QVERIFY(cm.currentReconnectAttempts() >= 1);

        // Manually fire the reconnect timer slot (host/port empty => no connect attempt)
        QVERIFY(QMetaObject::invokeMethod(&cm, "onReconnectTimer", Qt::DirectConnection));
        QVERIFY(cm.currentReconnectAttempts() >= 1);
    }
};

QTEST_APPLESS_MAIN(TestConnectionManager)
#include "test_connection_manager.moc"
