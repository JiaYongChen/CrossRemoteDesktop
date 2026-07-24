#include <QtTest/QTest>
#include "client/core/TripleBuffer.h"
#include "client/core/FrameSlot.h"

class TestRefreshLatencyMetric : public QObject {
    Q_OBJECT
private slots:
    void testTripleBufferStoresFrame() {
        TripleBuffer<FrameSlot> tb;
        FrameSlot* w = nullptr;
        int idx = tb.acquireWrite(w);
        QVERIFY(idx >= 0);
        w->remoteSize = QSize(1920, 1080);
        tb.commitWrite(idx);

        FrameSlot* r = nullptr;
        int ridx = tb.getReadSlot(r);
        QVERIFY(ridx >= 0);
        QCOMPARE(r->remoteSize, QSize(1920, 1080));
    }

    void testTripleBufferEmptyReturnsNoSlot() {
        TripleBuffer<FrameSlot> tb;
        FrameSlot* r = nullptr;
        QCOMPARE(tb.getReadSlot(r), -1);
    }
};

QTEST_APPLESS_MAIN(TestRefreshLatencyMetric)
#include "test_refresh_latency_metric.moc"
