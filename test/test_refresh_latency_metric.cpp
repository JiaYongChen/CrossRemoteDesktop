#include <QtTest/QTest>
#include "../src/client/core/TripleBuffer.h"
#include "../src/client/managers/DecodeWorker.h"

class TestRefreshLatencyMetric : public QObject {
    Q_OBJECT
private slots:
    void testTripleBufferStoresFrame() {
        TripleBuffer<DecodeWorker::DecodedFrame> tb;
        DecodeWorker::DecodedFrame* w = nullptr;
        int idx = tb.acquireWrite(w);
        QVERIFY(idx >= 0);
        w->frameId = 1;
        w->remoteSize = QSize(1920, 1080);
        tb.commitWrite(idx);

        DecodeWorker::DecodedFrame* r = nullptr;
        int ridx = tb.getReadSlot(r);
        QVERIFY(ridx >= 0);
        QCOMPARE(r->frameId, quint64(1));
        QCOMPARE(r->remoteSize, QSize(1920, 1080));
    }

    void testTripleBufferEmptyReturnsNoSlot() {
        TripleBuffer<DecodeWorker::DecodedFrame> tb;
        DecodeWorker::DecodedFrame* r = nullptr;
        QCOMPARE(tb.getReadSlot(r), -1);
    }
};

QTEST_APPLESS_MAIN(TestRefreshLatencyMetric)
#include "test_refresh_latency_metric.moc"
