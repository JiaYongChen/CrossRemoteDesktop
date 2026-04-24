#include <QtTest/QTest>
#include <QtGui/QImage>
#include <chrono>

#include "../src/client/managers/SessionManager.h"

class TestRefreshLatencyMetric : public QObject {
    Q_OBJECT
private slots:
    void testDequeueReturnsArrivalTimestamp() {
        SessionManager sm(QStringLiteral("test-conn"));
        // Enqueue is private; drive through handleScreenData path later.
        // For now: validate the new dequeueScreenFrame API signature compiles
        // and returns false on empty queue.
        QImage out;
        std::chrono::steady_clock::time_point ts{};
        QVERIFY(!sm.dequeueScreenFrame(out, ts));
    }
};

QTEST_MAIN(TestRefreshLatencyMetric)
#include "test_refresh_latency_metric.moc"
