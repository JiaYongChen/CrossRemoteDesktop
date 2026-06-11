#include <QtTest/QTest>
#include "../src/client/managers/SessionManager.h"

class TestSessionManagerLogic : public QObject {
    Q_OBJECT
private slots:
    void test_setFrameRate_validRange() {
        SessionManager sm("test");
        sm.setFrameRate(30);
        QCOMPARE(sm.frameRate(), 30);
        sm.setFrameRate(1);
        QCOMPARE(sm.frameRate(), 1);
        sm.setFrameRate(120);
        QCOMPARE(sm.frameRate(), 120);
    }

    void test_setFrameRate_belowMin() {
        SessionManager sm("test");
        sm.setFrameRate(0);
        QCOMPARE(sm.frameRate(), 1);
    }

    void test_setFrameRate_aboveMax() {
        SessionManager sm("test");
        sm.setFrameRate(200);
        QCOMPARE(sm.frameRate(), 120);
    }

    void test_setFrameRate_negative() {
        SessionManager sm("test");
        sm.setFrameRate(-5);
        QCOMPARE(sm.frameRate(), 1);
    }

    void test_resetStats() {
        SessionManager sm("test");
        sm.resetStats();
        auto stats = sm.performanceStats();
        QCOMPARE(stats.currentFPS, 0.0);
        QCOMPARE(stats.frameCount, 0);
        QVERIFY(!stats.sessionStartTime.isValid());
    }

    void test_getFormattedPerformanceInfo() {
        SessionManager sm("test");
        sm.setFrameRate(30);
        QString info = sm.getFormattedPerformanceInfo();
        QVERIFY(info.contains("FPS:"));
        QVERIFY(info.contains("Frame Rate: 30"));
    }

    void test_remoteScreenSize_default() {
        SessionManager sm("test");
        QCOMPARE(sm.remoteScreenSize(), QSize());
    }

    void test_connectionId() {
        SessionManager sm("session-001");
        QCOMPARE(sm.connectionId(), QString("session-001"));
    }

    void test_isActive_default() {
        SessionManager sm("test");
        QVERIFY(!sm.isActive());
    }
};

QTEST_MAIN(TestSessionManagerLogic)
#include "test_sessionmanager_logic.moc"
