#include <QtTest/QTest>
#include <QtGui/QImage>
#include <chrono>

#include "../src/client/core/TripleBuffer.h"
#include "../src/client/core/FrameSlot.h"

class TestRefreshLatencyMetric : public QObject {
    Q_OBJECT
private slots:
    void testTripleBufferReturnsArrivalTimestamp() {
        // Validate that the TripleBuffer exposes FrameSlot with arrivalTs
        TripleBuffer<FrameSlot> tb;
        FrameSlot* w = nullptr;
        int idx = tb.acquireWrite(w);
        QVERIFY(idx >= 0);
        auto beforeWrite = std::chrono::steady_clock::now();
        w->image = QImage(16, 16, QImage::Format_RGB888);
        w->image.fill(Qt::red);
        w->arrivalTs = std::chrono::steady_clock::now();
        tb.commitWrite(idx);

        FrameSlot* r = nullptr;
        int ridx = tb.getReadSlot(r);
        QVERIFY(ridx >= 0);
        QVERIFY(r != nullptr);
        QVERIFY(!r->image.isNull());
        // arrivalTs should be between beforeWrite and now
        QVERIFY(r->arrivalTs >= beforeWrite);
        QVERIFY(r->arrivalTs <= std::chrono::steady_clock::now());
    }

    void testTripleBufferEmptyReturnsNoSlot() {
        TripleBuffer<FrameSlot> tb;
        FrameSlot* r = nullptr;
        QCOMPARE(tb.getReadSlot(r), -1);
    }
};

QTEST_MAIN(TestRefreshLatencyMetric)
#include "test_refresh_latency_metric.moc"
