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

    void testCaptureQueueFIFO() {
        QVERIFY(m_qm->initialize(10, 5));

        for ( quint64 i = 1; i <= 5; ++i ) {
            QVERIFY(m_qm->enqueueCapturedFrame(makeFrame(i)));
        }

        for ( quint64 i = 1; i <= 5; ++i ) {
            CapturedFrame f;
            QVERIFY(m_qm->dequeueCapturedFrame(f));
            QCOMPARE(f.frameId, i);
        }
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
        QVERIFY(m_qm->isQueueHealthy(QueueManager::CaptureQueue));
        QVERIFY(m_qm->isQueueHealthy(QueueManager::ProcessedQueue));
    }

    // --- Stop/restart ---

    void testStopAndRestart() {
        QVERIFY(m_qm->initialize(5, 5));

        m_qm->stopAllQueues();

        // After stop, enqueue should fail
        QVERIFY(!m_qm->enqueueCapturedFrame(makeFrame(1)));

        m_qm->restartAllQueues();
        // After restart, enqueue and dequeue should both succeed
        QVERIFY(m_qm->enqueueCapturedFrame(makeFrame(2)));
        CapturedFrame f;
        QVERIFY(m_qm->dequeueCapturedFrame(f));
        QCOMPARE(f.frameId, quint64(2));
    }

    // --- Concurrent enqueue/dequeue ---

    void testConcurrentAccess() {
        QVERIFY(m_qm->initialize(200, 200));

        constexpr int COUNT = 50;
        std::atomic<int> produced{0};
        std::atomic<int> consumed{0};

        // Producer thread: enqueue COUNT frames
        QThread* producer = QThread::create([this, &produced]() {
            for ( int i = 0; i < COUNT; ++i ) {
                m_qm->enqueueCapturedFrame(makeFrame(static_cast<quint64>(i + 1)));
                produced.fetch_add(1);
            }
        });

        producer->start();
        producer->wait(10000);
        QCOMPARE(produced.load(), COUNT);

        // Consumer thread: dequeue all frames (non-blocking tryDequeue)
        QThread* consumer = QThread::create([this, &consumed]() {
            CapturedFrame f;
            while ( consumed.load() < COUNT ) {
                if ( m_qm->dequeueCapturedFrame(f) ) {
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
