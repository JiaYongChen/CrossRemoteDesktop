#include <QtTest/QTest>
#include "client/core/TripleBuffer.h"
#include "client/core/FrameSlot.h"

class TestSessionLatestWins : public QObject {
    Q_OBJECT
private slots:
    void testTripleBufferLatestWins() {
        TripleBuffer<FrameSlot> tb;
        for (int i = 0; i < 3; ++i) {
            FrameSlot* w = nullptr;
            int idx = tb.acquireWrite(w);
            QVERIFY(idx >= 0 && idx <= 2);
            // 用 remoteSize 宽度区分先后提交的帧
            w->remoteSize = QSize(i + 1, 1);
            tb.commitWrite(idx);
        }
        FrameSlot* r = nullptr;
        int rs = tb.getReadSlot(r);
        QVERIFY(rs >= 0);
        QCOMPARE(r->remoteSize, QSize(3, 1));
    }

    void testDecodeWorkerExposesFrameSlot() {
        FrameSlot frame;
        QVERIFY(frame.image.isNull());
        QVERIFY(frame.remoteSize.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestSessionLatestWins)
#include "test_session_latest_wins.moc"
