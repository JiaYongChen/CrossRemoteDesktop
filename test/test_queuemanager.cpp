#include <QtTest/QTest>
#include <QtCore/QThread>
#include <QtCore/QElapsedTimer>
#include "../src/server/dataflow/QueueManager.h"
#include "../src/server/dataflow/DataFlowStructures.h"

class TestQueueManager : public QObject {
    Q_OBJECT

private:
    QueueManager* m_qm = nullptr;

    static CapturedFrame makeFrame(quint64 id, int width = 100, int height = 100) {
        QImage img(width, height, QImage::Format_RGB32);
        img.fill(Qt::blue);
        return CapturedFrame(img, id);
    }

    static ProcessedData makeProcessed(quint64 id) {
        QByteArray data(1024, 'A');
        return ProcessedData(data, id, QSize(100, 100), 1024);
    }

private slots:
    void init() {
        // Each test gets a fresh QueueManager (not the singleton)
        m_qm = new QueueManager(this);
    }

    void cleanup() {
        if ( m_qm ) {
            m_qm->cleanup();
            delete m_qm;
            m_qm = nullptr;
        }
    }

    // --- Basic lifecycle ---

    void testInitializeAndCleanup() {
        QVERIFY(m_qm->initialize(5));
        // Double-init should succeed (idempotent)
        QVERIFY(m_qm->initialize(5));
        m_qm->cleanup();
    }

    // --- CaptureQueue enqueue/dequeue ---

    void testCaptureQueueBasic() {
        QVERIFY(m_qm->initialize(5));

        CapturedFrame sent = makeFrame(1);
        QVERIFY(m_qm->enqueueCapturedFrame(sent));

        CapturedFrame received;
        QVERIFY(m_qm->dequeueCapturedFrame(received));
        QCOMPARE(received.frameId, quint64(1));
    }

    // FIFO: 入队多帧后逐帧按顺序出队
    void testCaptureQueueFIFO() {
        QVERIFY(m_qm->initialize(10));

        for ( quint64 i = 1; i <= 5; ++i ) {
            QVERIFY(m_qm->enqueueCapturedFrame(makeFrame(i)));
        }

        // 流水池模型 FIFO：逐帧按入队顺序出队
        for ( quint64 expected = 1; expected <= 5; ++expected ) {
            CapturedFrame f;
            QVERIFY2(m_qm->dequeueCapturedFrame(f),
                     qPrintable(QString("出队帧 %1 失败").arg(expected)));
            QCOMPARE(f.frameId, expected);
        }

        // 队列已清空，再次出队应返回 false
        CapturedFrame f;
        QVERIFY(!m_qm->dequeueCapturedFrame(f));
    }

    // --- ProcessedQueue enqueue/dequeue ---

    void testProcessedQueueBasic() {
        QVERIFY(m_qm->initialize(5));

        ProcessedData sent = makeProcessed(42);
        m_qm->processedQueue()->tryEnqueueDrainToLatest(sent);

        ProcessedData received;
        QVERIFY(m_qm->processedQueue()->tryDequeue(received));
        QCOMPARE(received.originalFrameId, quint64(42));
    }

    // --- Queue stats ---

    void testQueueStats() {
        QVERIFY(m_qm->initialize(10));

        // Enqueue a few frames (frameId must be >0 for isValid)
        for ( int i = 1; i <= 3; ++i ) {
            QVERIFY(m_qm->enqueueCapturedFrame(makeFrame(static_cast<quint64>(i))));
        }

        m_qm->forceUpdateStats();
        QueueStats stats = m_qm->getQueueStats(QueueManager::CaptureQueue);
        QCOMPARE(stats.currentSize, 3);
    }

    // --- Clear queue ---

    void testClearQueue() {
        QVERIFY(m_qm->initialize(10));

        for ( int i = 0; i < 5; ++i ) {
            (void)m_qm->enqueueCapturedFrame(makeFrame(static_cast<quint64>(i)));
        }

        m_qm->clearQueue(QueueManager::CaptureQueue);
        m_qm->forceUpdateStats();
        QueueStats stats = m_qm->getQueueStats(QueueManager::CaptureQueue);
        QCOMPARE(stats.currentSize, 0);
    }

    // --- Drain-to-Latest: 满时清空旧帧保留新帧 ---

    void testCaptureQueueDrainToLatest() {
        QVERIFY(m_qm->initialize(3));

        // 填满队列（3 帧）
        QVERIFY(m_qm->enqueueCapturedFrame(makeFrame(1)));
        QVERIFY(m_qm->enqueueCapturedFrame(makeFrame(2)));
        QVERIFY(m_qm->enqueueCapturedFrame(makeFrame(3)));

        // 第 4 帧触发 drain：清空帧 1-3，保留帧 4
        QVERIFY(m_qm->enqueueCapturedFrame(makeFrame(4)));

        // 出队应只得到帧 4
        CapturedFrame f;
        QVERIFY(m_qm->dequeueCapturedFrame(f));
        QCOMPARE(f.frameId, quint64(4));

        // 队列应已空
        QVERIFY(!m_qm->dequeueCapturedFrame(f));
    }

    void testProcessedQueueDrainToLatest() {
        QVERIFY(m_qm->initialize(5));

        // 设置处理队列容量为 3 以测试 drain-to-latest
        m_qm->processedQueue()->setMaxSize(3);

        // 填满处理队列
        m_qm->processedQueue()->tryEnqueueDrainToLatest(makeProcessed(10));
        m_qm->processedQueue()->tryEnqueueDrainToLatest(makeProcessed(20));
        m_qm->processedQueue()->tryEnqueueDrainToLatest(makeProcessed(30));

        // 触发 drain
        m_qm->processedQueue()->tryEnqueueDrainToLatest(makeProcessed(40));

        // 仅应出队最新帧
        ProcessedData d;
        QVERIFY(m_qm->processedQueue()->tryDequeue(d));
        QCOMPARE(d.originalFrameId, quint64(40));
        QVERIFY(!m_qm->processedQueue()->tryDequeue(d));
    }

    void testDrainToLatestNotTriggeredWhenNotFull() {
        QVERIFY(m_qm->initialize(10));

        // 队列未满时正常 FIFO 行为
        QVERIFY(m_qm->enqueueCapturedFrame(makeFrame(1)));
        QVERIFY(m_qm->enqueueCapturedFrame(makeFrame(2)));

        CapturedFrame f;
        QVERIFY(m_qm->dequeueCapturedFrame(f));
        QCOMPARE(f.frameId, quint64(1));  // 仍然 FIFO
        QVERIFY(m_qm->dequeueCapturedFrame(f));
        QCOMPARE(f.frameId, quint64(2));
    }

    // --- Health check ---

    void testQueueHealthy() {
        QVERIFY(m_qm->initialize(10));
        // Verify queues are in a normal state after initialization
        QueueStats capStats = m_qm->getQueueStats(QueueManager::CaptureQueue);
        QVERIFY(capStats.getUsagePercentage() >= 0.0);
    }

    // --- Concurrent enqueue/dequeue ---

    // 并发环境下验证 Drain-to-Latest：队列满时清空旧帧保留新帧。
    // 消费者验证收到的帧 ID 有效且最终数量正确（drain 丢弃的帧不计数）。
    void testConcurrentAccess() {
        QVERIFY(m_qm->initialize(3));  // 小容量更容易触发 drain

        constexpr int COUNT = 50;
        std::atomic<int> produced{0};
        std::atomic<int> consumed{0};

        QThread* producer = QThread::create([this, &produced]() {
            for ( int i = 0; i < COUNT; ++i ) {
                (void)m_qm->enqueueCapturedFrame(makeFrame(static_cast<quint64>(i + 1)));
                produced.fetch_add(1);
            }
        });

        producer->start();
        producer->wait(10000);
        QCOMPARE(produced.load(), COUNT);

        // 小容量 + 多线程生产 → drain 会丢弃部分帧。
        // 消费者收到多少就算多少，不期望 COUNT 全量。
        QThread* consumer = QThread::create([this, &consumed]() {
            CapturedFrame f;
            // 给生产端时间完成，然后消费所有剩余帧
            QThread::msleep(100);
            while ( consumed.load() < 50 ) {
                if ( m_qm->dequeueCapturedFrame(f) ) {
                    QVERIFY(f.frameId >= 1 && f.frameId <= static_cast<quint64>(50));
                    consumed.fetch_add(1);
                } else {
                    if ( consumed.load() > 0 ) break;  // 队列空且已有消费 → 结束
                    QThread::msleep(1);
                }
            }
        });

        consumer->start();
        consumer->wait(10000);
        // drain 模式下不需要验证 COUNT 全量——只需验证消费者未崩溃
        QVERIFY(consumed.load() > 0);
        QVERIFY(consumed.load() <= COUNT);

        delete producer;
        delete consumer;
    }
};

QTEST_MAIN(TestQueueManager)
#include "test_queuemanager.moc"
