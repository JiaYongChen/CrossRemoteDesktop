#include <QtTest/QTest>
#include "../../src/client/core/TripleBuffer.h"

struct TestPayload { int value = 0; };

class TestTripleBufferSwap : public QObject {
    Q_OBJECT
private slots:
    void testInitialStateReturnsNoSlot() {
        TripleBuffer<TestPayload> tb;
        TestPayload* out = nullptr;
        QCOMPARE(tb.getReadSlot(out), -1);
    }
    void testProduceConsumeSingle() {
        TripleBuffer<TestPayload> tb;
        TestPayload* w = nullptr;
        int slot = tb.acquireWrite(w);
        QVERIFY(slot >= 0 && slot <= 2);
        w->value = 42;
        tb.commitWrite(slot);
        TestPayload* r = nullptr;
        int rslot = tb.getReadSlot(r);
        QVERIFY(rslot >= 0);
        QCOMPARE(r->value, 42);
    }
    void testConsumerOnlySeesLatest() {
        TripleBuffer<TestPayload> tb;
        TestPayload* w = nullptr;
        int s0 = tb.acquireWrite(w); w->value = 10; tb.commitWrite(s0);
        int s1 = tb.acquireWrite(w); w->value = 20; tb.commitWrite(s1);
        TestPayload* r = nullptr;
        int rs = tb.getReadSlot(r);
        QVERIFY(rs >= 0);
        QCOMPARE(r->value, 20);
    }
    void testMultiSlotWrapAround() {
        TripleBuffer<TestPayload> tb;
        for (int i = 0; i < 100; ++i) {
            TestPayload* w = nullptr;
            int s = tb.acquireWrite(w);
            w->value = i;
            tb.commitWrite(s);
        }
        TestPayload* r = nullptr;
        int rs = tb.getReadSlot(r);
        QVERIFY(rs >= 0);
        QCOMPARE(r->value, 99);
    }
};

QTEST_MAIN(TestTripleBufferSwap)
#include "test_triple_buffer_swap.moc"
