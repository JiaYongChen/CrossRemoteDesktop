#include <QtTest/QTest>
#include "../../src/client/core/TripleBuffer.h"
#include "../../src/client/core/FrameSlot.h"

class TestSessionLatestWins : public QObject {
    Q_OBJECT
private slots:
    void testTripleBufferLatestWins() {
        TripleBuffer<FrameSlot> tb;
        for (int i = 0; i < 3; ++i) {
            FrameSlot* w = nullptr;
            int idx = tb.acquireWrite(w);
            QVERIFY(idx >= 0 && idx <= 2);
            w->frameId = static_cast<quint64>(i + 1);
            tb.commitWrite(idx);
        }
        FrameSlot* r = nullptr;
        int rs = tb.getReadSlot(r);
        QVERIFY(rs >= 0);
        QCOMPARE(r->frameId, quint64(3));
    }

    void testDecodeWorkerExposesFrameSlot() {
        FrameSlot frame;
        frame.frameId = 42;
        QCOMPARE(frame.frameId, quint64(42));
        QVERIFY(frame.image.isNull());
        QVERIFY(frame.remoteSize.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestSessionLatestWins)
#include "test_session_latest_wins.moc"
