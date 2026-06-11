#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include "test_sessionmanager_common.h"
#include "../src/client/managers/SessionManager.h"

class TestSessionManagerLifecycle : public QObject {
    Q_OBJECT
private slots:
    void test_terminateSession_cleanup() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockAuthenticated = true;
        sm.startSession();
        QVERIFY(sm.isActive());

        sm.terminateSession();
        QVERIFY(!sm.isActive());
        QCOMPARE(sm.remoteScreenSize(), QSize());
    }

    void test_isActive_authenticated_and_active() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        QVERIFY(!sm.isActive());

        mock->m_mockAuthenticated = true;
        sm.startSession();
        QVERIFY(sm.isActive());
    }

    void test_isActive_notAuthenticated() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockAuthenticated = false;
        QVERIFY(!sm.isActive());
    }

    void test_suspendSession_stopsTimer() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockAuthenticated = true;
        sm.startSession();
        QVERIFY(sm.isActive());

        sm.suspendSession();
        QVERIFY(!sm.isActive());
    }

    void test_resumeSession_restartsTimer() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockAuthenticated = true;
        sm.startSession();
        sm.suspendSession();
        QVERIFY(!sm.isActive());

        sm.resumeSession();
        QVERIFY(sm.isActive());
        QVERIFY(mock->messageSent);
    }

    void test_resetConnection_emits_signal() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);

        QSignalSpy spy(&sm, &SessionManager::connectionReset);
        sm.resetConnection();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(sm.remoteScreenSize(), QSize());
        auto stats = sm.performanceStats();
        QCOMPARE(stats.frameCount, 0);
    }

    void test_destroyDecodePipeline_withoutCreate() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);

        sm.destroyDecodePipeline();
        // 通过：空调用直接返回，无崩溃
    }

    void test_sendInputEvent_whenDisconnected() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockConnected = false;

        sm.sendMouseEvent(100, 200, 0);
        QVERIFY(!mock->messageSent);
    }

    void test_sendInputEvent_whenConnected() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockConnected = true;
        mock->m_mockAuthenticated = true;
        sm.startSession();

        sm.sendMouseEvent(100, 200, 1);
        QVERIFY(mock->messageSent);
        QCOMPARE(mock->lastSentType, MessageType::MOUSE_EVENT);
    }

    void test_terminateSession_stopsPipeline() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockAuthenticated = true;
        sm.startSession();
        QVERIFY(sm.isActive());

        sm.terminateSession();
        sm.destroyDecodePipeline();
        // 通过：管线已清理，再次调用安全
    }
};

QTEST_MAIN(TestSessionManagerLifecycle)
#include "test_sessionmanager_lifecycle.moc"
