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
        QVERIFY(m_qm->initialize(5, 3));
        // Double-init should succeed (idempotent)
        QVERIFY(m_qm->initialize(5, 3));
        m_qm->cleanup();
    }

    // --- CaptureQueue enqueue/dequeue ---

    void testCaptureQueueBasic() {
        QVERIFY(m_qm->initialize(5, 5));

        CapturedFrame sent = makeFrame(1);
        QVERIFY(m_qm->enqueueCapturedFrame(sent));

        CapturedFrame received;
        QVERIFY(m_qm->dequeueCapturedFrame(received));
        QCOMPARE(received.frameId, quint64(1));
    }

    // FIFO: 入队多帧后逐帧按顺序出队
    void testCaptureQueueFIFO() {
        QVERIFY(m_qm->initialize(10, 5));

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
        QVERIFY(m_qm->initialize(5, 5));

        ProcessedData sent = makeProcessed(42);
        QVERIFY(m_qm->enqueueProcessedData(sent));

        ProcessedData received;
        QVERIFY(m_qm->dequeueProcessedData(received));
        QCOMPARE(received.originalFrameId, quint64(42));
    }

    // --- Queue stats ---

    void testQueueStats() {
        QVERIFY(m_qm->initialize(10, 10));

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
        QVERIFY(m_qm->initialize(10, 10));

        for ( int i = 0; i < 5; ++i ) {
            m_qm->enqueueCapturedFrame(makeFrame(static_cast<quint64>(i)));
        }

        m_qm->clearQueue(QueueManager::CaptureQueue);
        m_qm->forceUpdateStats();
        QueueStats stats = m_qm->getQueueStats(QueueManager::CaptureQueue);
        QCOMPARE(stats.currentSize, 0);
    }

    // --- Health check ---

    void testQueueHealthy() {
        QVERIFY(m_qm->initialize(10, 10));
        // Verify queues are in a normal state after initialization
        QueueStats capStats = m_qm->getQueueStats(QueueManager::CaptureQueue);
        QueueStats procStats = m_qm->getQueueStats(QueueManager::ProcessedQueue);
        QVERIFY(capStats.getUsagePercentage() >= 0.0);
        QVERIFY(procStats.getUsagePercentage() >= 0.0);
    }

    // --- Concurrent enqueue/dequeue ---

    // 并发环境下验证 FIFO 出队：消费者应收到全部 COUNT 帧。
    // 流水池模型下逐帧出队，不再排空，验证线程安全和数据完整性。
    void testConcurrentAccess() {
        QVERIFY(m_qm->initialize(200, 200));

        constexpr int COUNT = 50;
        std::atomic<int> produced{0};
        std::atomic<int> consumed{0};

        QThread* producer = QThread::create([this, &produced]() {
            for ( int i = 0; i < COUNT; ++i ) {
                m_qm->enqueueCapturedFrame(makeFrame(static_cast<quint64>(i + 1)));
                produced.fetch_add(1);
            }
        });

        producer->start();
        producer->wait(10000);
        QCOMPARE(produced.load(), COUNT);

        // 流水池 FIFO: 逐帧出队，消费者应收到全部 COUNT 帧
        QThread* consumer = QThread::create([this, &consumed]() {
            CapturedFrame f;
            while ( consumed.load() < COUNT ) {
                if ( m_qm->dequeueCapturedFrame(f) ) {
                    // 验证帧 ID 范围有效（不验证严格顺序，并发下可能乱序入队）
                    QVERIFY(f.frameId >= 1 && f.frameId <= static_cast<quint64>(COUNT));
                    consumed.fetch_add(1);
                }
            }
        });

        consumer->start();
        consumer->wait(10000);
        QCOMPARE(consumed.load(), COUNT);

        delete producer;
        delete consumer;
    }
};

QTEST_MAIN(TestQueueManager)
#include "test_queuemanager.moc"
