/**
 * @file test_producer_consumer_integration.cpp
 * @brief 生产者-消费者模式集成测试
 *
 * 测试数据处理队列的生产者功能和ServerWorker的消费者功能，
 * 验证队列的线程安全性和数据传输的完整性。
 *
 * @author Assistant
 * @date 2024
 */

#include <QtTest/QtTest>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QLoggingCategory>
#include <QtGui/QImage>
#include <memory>

#include "../src/server/service/ServerWorker.h"
#include "../src/server/dataprocessing/DataProcessingWorker.h"
#include "../src/server/dataflow/QueueManager.h"
#include "../src/server/dataflow/DataFlowStructures.h"
#include "../src/common/core/threading/ThreadSafeQueue.h"
#include "../src/common/core/threading/ThreadManager.h"

Q_LOGGING_CATEGORY(lcProducerConsumerTest, "test.producer.consumer")

/**
 * @brief 生产者-消费者模式集成测试类
 *
 * 测试数据处理队列和ServerWorker之间的协作，
 * 验证生产者-消费者模式的正确性和性能。
 */
    class TestProducerConsumerIntegration : public QObject {
    Q_OBJECT

    public:
        TestProducerConsumerIntegration();
        ~TestProducerConsumerIntegration();

    private slots:
        /**
         * @brief 测试初始化
         */
        void initTestCase();

        /**
         * @brief 测试清理
         */
        void cleanupTestCase();

        /**
         * @brief 每个测试前的初始化
         */
        void init();

        /**
         * @brief 每个测试后的清理
         */
        void cleanup();

        /**
         * @brief 测试基本的生产者-消费者功能
         */
        void test_basicProducerConsumer();

        /**
         * @brief 测试队列的线程安全性
         */
        void test_queueThreadSafety();

        /**
         * @brief 测试数据完整性
         */
        void test_dataIntegrity();

        /**
         * @brief 测试队列满时的处理
         */
        void test_queueFullHandling();

        /**
         * @brief 测试队列空时的处理
         */
        void test_queueEmptyHandling();

        /**
         * @brief 测试高并发场景
         */
        void test_highConcurrency();

        /**
         * @brief 测试队列统计信息
         */
        void test_queueStatistics();

    private:
        /**
         * @brief 创建测试用的图像数据
         * @param width 图像宽度
         * @param height 图像高度
         * @param pattern 图像模式
         * @return 测试图像
         */
        QImage createTestImage(int width = 800, int height = 600, int pattern = 0);

        /**
         * @brief 创建测试用的捕获帧
         * @param frameId 帧ID
         * @param image 图像数据
         * @return 捕获帧
         */
        CapturedFrame createTestFrame(quint64 frameId, const QImage& image);

        /**
         * @brief 等待队列处理完成
         * @param maxWaitMs 最大等待时间（毫秒）
         * @return 是否成功等待
         */
        bool waitForQueueProcessing(int maxWaitMs = 5000);

        /**
         * @brief 验证处理后的数据
         * @param processedData 处理后的数据
         * @param originalFrame 原始帧
         * @return 验证是否通过
         */
        bool verifyProcessedData(const ProcessedData& processedData, const CapturedFrame& originalFrame);

    private:
        QueueManager* m_queueManager;                           ///< 队列管理器
        DataProcessingWorker* m_dataProcessor;                 ///< 数据处理器（生产者）
        ServerWorker* m_serverWorker;                          ///< 服务器工作器（消费者）
        ThreadManager* m_threadManager;                        ///< 线程管理器
        QThread* m_processingThread;                           ///< 处理线程
        QThread* m_serverThread;                               ///< 服务器线程

        int m_processedCount;                                  ///< 已处理数据计数
        int m_consumedCount;                                   ///< 已消费数据计数
        QMutex m_counterMutex;                                 ///< 计数器互斥锁
};

TestProducerConsumerIntegration::TestProducerConsumerIntegration()
    : m_queueManager(nullptr)
    , m_dataProcessor(nullptr)
    , m_serverWorker(nullptr)
    , m_threadManager(nullptr)
    , m_processingThread(nullptr)
    , m_serverThread(nullptr)
    , m_processedCount(0)
    , m_consumedCount(0) {
}

TestProducerConsumerIntegration::~TestProducerConsumerIntegration() {
}

void TestProducerConsumerIntegration::initTestCase() {
    qCDebug(lcProducerConsumerTest) << "初始化生产者-消费者集成测试";

    // 初始化线程管理器
    m_threadManager = ThreadManager::instance();
    QVERIFY(m_threadManager != nullptr);

    // 初始化队列管理器
    m_queueManager = QueueManager::instance();
    QVERIFY(m_queueManager != nullptr);

    // 初始化队列管理器（设置队列大小）
    bool initResult = m_queueManager->initialize(10, 5);
    QVERIFY(initResult);

    // 清空队列
    m_queueManager->clearQueue(QueueManager::CaptureQueue);
    m_queueManager->clearQueue(QueueManager::ProcessedQueue);
}

void TestProducerConsumerIntegration::cleanupTestCase() {
    qCDebug(lcProducerConsumerTest) << "清理生产者-消费者集成测试";

    // 清理线程
    if ( m_processingThread && m_processingThread->isRunning() ) {
        // 先在所属线程同步调用stop，确保DataProcessingWorker完成cleanup并安全停止内部定时器
        if ( m_dataProcessor ) {
            // 使用兼容写法，避免旧版Qt对lambda重载的支持不足
            QMetaObject::invokeMethod(m_dataProcessor, "stop",
                Qt::BlockingQueuedConnection,
                Q_ARG(bool, true));
        }
        m_processingThread->quit();
        m_processingThread->wait(3000);
    }

    if ( m_serverThread && m_serverThread->isRunning() ) {
        m_serverThread->quit();
        m_serverThread->wait(3000);
    }

    // 清理对象
    delete m_dataProcessor;
    delete m_serverWorker;
    delete m_processingThread;
    delete m_serverThread;

    // 清空队列
    m_queueManager->clearQueue(QueueManager::CaptureQueue);
    m_queueManager->clearQueue(QueueManager::ProcessedQueue);
}

void TestProducerConsumerIntegration::init() {
    // 重置计数器
    QMutexLocker locker(&m_counterMutex);
    m_processedCount = 0;
    m_consumedCount = 0;
}

void TestProducerConsumerIntegration::cleanup() {
    // 清空队列
    m_queueManager->clearQueue(QueueManager::CaptureQueue);
    m_queueManager->clearQueue(QueueManager::ProcessedQueue);
}

void TestProducerConsumerIntegration::test_basicProducerConsumer() {
    qCDebug(lcProducerConsumerTest) << "测试基本的生产者-消费者功能";

    // 创建测试数据
    QImage testImage = createTestImage(400, 300, 1);
    CapturedFrame testFrame = createTestFrame(1, testImage);

    // 验证队列初始状态
    auto captureStats = m_queueManager->getQueueStats(QueueManager::CaptureQueue);
    auto processedStats = m_queueManager->getQueueStats(QueueManager::ProcessedQueue);
    QCOMPARE(captureStats.currentSize, 0);
    QCOMPARE(processedStats.currentSize, 0);

    // 生产者：添加数据到捕获队列
    bool enqueued = m_queueManager->enqueueCapturedFrame(testFrame);
    QVERIFY(enqueued);
    
    captureStats = m_queueManager->getQueueStats(QueueManager::CaptureQueue);
    QCOMPARE(captureStats.currentSize, 1);

    // 创建数据处理器（生产者）
    m_dataProcessor = new DataProcessingWorker();
    m_processingThread = new QThread();
    m_dataProcessor->moveToThread(m_processingThread);

    // 连接信号
    connect(m_processingThread, &QThread::started, m_dataProcessor, &DataProcessingWorker::start);
    connect(m_dataProcessor, &DataProcessingWorker::processingStatsUpdated, this,
        [this](quint64 processedFrames, quint64 droppedFrames, double averageLatency, double processingRate) {
        Q_UNUSED(droppedFrames)
            Q_UNUSED(averageLatency)
            Q_UNUSED(processingRate)
            QMutexLocker locker(&m_counterMutex);
        m_processedCount = static_cast<int>(processedFrames);
        qCDebug(lcProducerConsumerTest) << "数据处理统计更新，已处理帧数:" << processedFrames;
    });

    // 启动处理线程
    m_processingThread->start();

    // 等待处理完成
    QVERIFY(waitForQueueProcessing(3000));

    // 验证处理结果
    captureStats = m_queueManager->getQueueStats(QueueManager::CaptureQueue);
    QCOMPARE(captureStats.currentSize, 0);
    
    processedStats = m_queueManager->getQueueStats(QueueManager::ProcessedQueue);
    QCOMPARE(processedStats.currentSize, 1);

    // 验证处理后的数据
    ProcessedData processedData;
    bool dequeued = m_queueManager->dequeueProcessedData(processedData);
    QVERIFY(dequeued);
    QVERIFY(verifyProcessedData(processedData, testFrame));

    // 在退出线程前，确保在所属线程同步调用stop以触发cleanup，
    // 避免跨线程析构导致内部QTimer停止时出现killTimer警告
    if ( m_dataProcessor && m_processingThread && m_processingThread->isRunning() ) {
        QMetaObject::invokeMethod(m_dataProcessor, "stop",
            Qt::BlockingQueuedConnection,
            Q_ARG(bool, true));
    }
    m_processingThread->quit();
    m_processingThread->wait(3000);
}

void TestProducerConsumerIntegration::test_queueThreadSafety() {
    qCDebug(lcProducerConsumerTest) << "测试队列的线程安全性";

    const int numProducers = 3;
    const int numConsumers = 2;
    const int itemsPerProducer = 10;

    QList<QThread*> producerThreads;
    QList<QThread*> consumerThreads;
    QList<QObject*> workers; // 保存worker对象以便清理

    // 创建生产者线程
    for ( int i = 0; i < numProducers; ++i ) {
        QThread* thread = new QThread();
        QObject* worker = new QObject();
        workers.append(worker);
        worker->moveToThread(thread);
        producerThreads.append(thread);

        connect(thread, &QThread::started, [this, i]() {
            for ( int j = 0; j < 10; ++j ) {
                QImage image = createTestImage(200, 150, i * 100 + j);
                CapturedFrame frame = createTestFrame(i * 10 + j, image);

                // 使用重试循环模拟超时入队
                bool success = false;
                int retries = 100; // 1000ms / 10ms = 100次
                for (int retry = 0; retry < retries && !success; ++retry) {
                    success = m_queueManager->enqueueCapturedFrame(frame);
                    if (!success) {
                        QThread::msleep(10);
                    }
                }
                
                if ( success ) {
                    QMutexLocker locker(&m_counterMutex);
                    m_processedCount++;
                }

                QThread::msleep(10); // 模拟处理时间
            }
            // 生产者完成后退出线程
            QThread::currentThread()->quit();
        });

        thread->start();
    }

    // 创建消费者线程
    for ( int i = 0; i < numConsumers; ++i ) {
        QThread* thread = new QThread();
        QObject* worker = new QObject();
        workers.append(worker);
        worker->moveToThread(thread);
        consumerThreads.append(thread);

        connect(thread, &QThread::started, [this]() {
            CapturedFrame frame;
            while ( true ) {
                // 使用重试循环模拟超时出队
                bool success = false;
                int retries = 10; // 100ms / 10ms = 10次
                for (int retry = 0; retry < retries && !success; ++retry) {
                    success = m_queueManager->dequeueCapturedFrame(frame);
                    if (!success) {
                        QThread::msleep(10);
                    }
                }
                
                if ( success ) {
                    QMutexLocker locker(&m_counterMutex);
                    m_consumedCount++;
                } else {
                    // 检查是否所有生产者都完成了
                    QMutexLocker locker(&m_counterMutex);
                    auto stats = m_queueManager->getQueueStats(QueueManager::CaptureQueue);
                    if ( m_processedCount >= 30 && stats.currentSize == 0 ) { // 3 * 10 = 30
                        break;
                    }
                }
            }
            // 消费者完成后退出线程
            QThread::currentThread()->quit();
        });

        thread->start();
    }

    // 等待所有生产者线程完成
    for ( QThread* thread : producerThreads ) {
        if ( !thread->wait(5000) ) {
            thread->terminate();
            thread->wait(1000);
        }
    }

    // 等待所有消费者线程完成
    for ( QThread* thread : consumerThreads ) {
        if ( !thread->wait(5000) ) {
            thread->terminate();
            thread->wait(1000);
        }
    }

    // 清理worker对象
    for ( QObject* worker : workers ) {
        delete worker;
    }

    // 清理线程对象
    for ( QThread* thread : producerThreads ) {
        delete thread;
    }
    for ( QThread* thread : consumerThreads ) {
        delete thread;
    }

    // 验证结果
    QMutexLocker locker(&m_counterMutex);
    QCOMPARE(m_processedCount, numProducers * itemsPerProducer);
    QCOMPARE(m_consumedCount, numProducers * itemsPerProducer);
    auto stats = m_queueManager->getQueueStats(QueueManager::CaptureQueue);
    QVERIFY(stats.currentSize == 0);
}

void TestProducerConsumerIntegration::test_dataIntegrity() {
    qCDebug(lcProducerConsumerTest) << "测试数据完整性";

    // 创建多个不同的测试图像
    QList<CapturedFrame> testFrames;
    for ( int i = 0; i < 5; ++i ) {
        QImage image = createTestImage(300 + i * 50, 200 + i * 30, i);
        CapturedFrame frame = createTestFrame(i + 1, image); // 帧ID从1开始
        testFrames.append(frame);

        bool enqueued = m_queueManager->enqueueCapturedFrame(frame);
        QVERIFY(enqueued);
    }

    // 创建数据处理器
    m_dataProcessor = new DataProcessingWorker();
    m_processingThread = new QThread();
    m_dataProcessor->moveToThread(m_processingThread);

    QList<ProcessedData> processedResults;
    // 连接数据处理器信号
    connect(m_processingThread, &QThread::started, m_dataProcessor, &DataProcessingWorker::start);
    connect(m_dataProcessor, &DataProcessingWorker::processingStatsUpdated, this,
        [](quint64 processedFrames, quint64 droppedFrames, double averageLatency, double processingRate) {
        Q_UNUSED(droppedFrames)
            Q_UNUSED(averageLatency)
            Q_UNUSED(processingRate)
            qCDebug(lcProducerConsumerTest) << "处理统计更新: 已处理帧数" << processedFrames;
    });

    // 启动处理
    m_processingThread->start();

    // 等待处理完成
    QVERIFY(waitForQueueProcessing(5000));

    // 从处理队列中收集结果
    ProcessedData processedData;
    while ( m_queueManager->dequeueProcessedData(processedData) ) {
        processedResults.append(processedData);
    }

    // 验证所有数据都被处理
    QCOMPARE(processedResults.size(), testFrames.size());

    // 验证每个处理结果的完整性
    for ( const ProcessedData& processed : processedResults ) {
        bool found = false;
        for ( const CapturedFrame& original : testFrames ) {
            if ( original.frameId == processed.originalFrameId ) {
                QVERIFY(verifyProcessedData(processed, original));
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    // 在退出线程前，同步调用stop以确保cleanup在所属线程执行
    if ( m_dataProcessor && m_processingThread && m_processingThread->isRunning() ) {
        QMetaObject::invokeMethod(m_dataProcessor, "stop",
            Qt::BlockingQueuedConnection,
            Q_ARG(bool, true));
    }
    m_processingThread->quit();
    m_processingThread->wait(3000);
}

void TestProducerConsumerIntegration::test_queueFullHandling() {
    qCDebug(lcProducerConsumerTest) << "测试队列满时的处理";

    // 设置小容量队列进行测试
    ThreadSafeQueue<CapturedFrame> smallQueue(3);

    // 填满队列
    for ( int i = 0; i < 3; ++i ) {
        QImage image = createTestImage(100, 100, i);
        CapturedFrame frame = createTestFrame(i, image);
        bool enqueued = smallQueue.enqueue(frame);
        QVERIFY(enqueued);
    }

    QVERIFY(smallQueue.isFull());

    // 尝试添加更多数据（应该失败或阻塞）
    QImage extraImage = createTestImage(100, 100, 99);
    CapturedFrame extraFrame = createTestFrame(99, extraImage);

    // 使用非阻塞方式，应该失败
    bool enqueued = smallQueue.tryEnqueue(extraFrame);
    QVERIFY(!enqueued);

    // 使用超时方式，应该超时
    bool enqueuedWithTimeout = smallQueue.enqueue(extraFrame, 100); // 100ms超时
    QVERIFY(!enqueuedWithTimeout);

    // 消费一个元素后应该能够添加
    CapturedFrame dequeuedFrame;
    bool dequeued = smallQueue.tryDequeue(dequeuedFrame);
    QVERIFY(dequeued);

    // 现在应该能够添加
    enqueued = smallQueue.tryEnqueue(extraFrame);
    QVERIFY(enqueued);
}

void TestProducerConsumerIntegration::test_queueEmptyHandling() {
    qCDebug(lcProducerConsumerTest) << "测试队列空时的处理";

    // 确保队列为空
    m_queueManager->clearQueue(QueueManager::CaptureQueue);
    auto stats = m_queueManager->getQueueStats(QueueManager::CaptureQueue);
    QVERIFY(stats.currentSize == 0);

    // 尝试从空队列获取数据
    CapturedFrame frame;

    // 非阻塞方式应该失败
    bool dequeued = m_queueManager->dequeueCapturedFrame(frame);
    QVERIFY(!dequeued);

    // 超时方式模拟（使用重试循环）
    bool dequeuedWithTimeout = false;
    int retries = 10; // 100ms / 10ms = 10次
    for (int retry = 0; retry < retries && !dequeuedWithTimeout; ++retry) {
        dequeuedWithTimeout = m_queueManager->dequeueCapturedFrame(frame);
        if (!dequeuedWithTimeout) {
            QThread::msleep(10);
        }
    }
    QVERIFY(!dequeuedWithTimeout);

    // 添加数据后应该能够获取
    QImage testImage = createTestImage(200, 150, 1);
    CapturedFrame testFrame = createTestFrame(1, testImage);
    bool enqueued = m_queueManager->enqueueCapturedFrame(testFrame);
    QVERIFY(enqueued);

    // 现在应该能够获取数据
    dequeued = m_queueManager->dequeueCapturedFrame(frame);
    QVERIFY(dequeued);
    QCOMPARE(frame.frameId, testFrame.frameId);
}

void TestProducerConsumerIntegration::test_highConcurrency() {
    qCDebug(lcProducerConsumerTest) << "测试高并发场景";

    const int numThreads = 8;
    QList<QThread*> threads;
    QList<QObject*> workers; // 保存worker对象以便清理

    // 创建多个并发线程同时读写队列
    for ( int i = 0; i < numThreads; ++i ) {
        QThread* thread = new QThread();
        QObject* worker = new QObject();
        workers.append(worker);
        worker->moveToThread(thread);
        threads.append(thread);

        connect(thread, &QThread::started, [this, i]() {
            // 每个线程既是生产者又是消费者
            for ( int j = 0; j < 20; ++j ) {
                // 生产数据
                QImage image = createTestImage(150, 100, i * 20 + j);
                CapturedFrame frame = createTestFrame(i * 20 + j, image);
                
                // 使用重试循环模拟超时入队
                bool enqueued = false;
                int retries = 100; // 1000ms / 10ms = 100次
                for (int retry = 0; retry < retries && !enqueued; ++retry) {
                    enqueued = m_queueManager->enqueueCapturedFrame(frame);
                    if (!enqueued) {
                        QThread::msleep(10);
                    }
                }

                // 消费数据
                CapturedFrame consumedFrame;
                bool dequeued = false;
                retries = 10; // 100ms / 10ms = 10次
                for (int retry = 0; retry < retries && !dequeued; ++retry) {
                    dequeued = m_queueManager->dequeueCapturedFrame(consumedFrame);
                    if (!dequeued) {
                        QThread::msleep(10);
                    }
                }
                
                if ( dequeued ) {
                    QMutexLocker locker(&m_counterMutex);
                    m_consumedCount++;
                }

                QThread::msleep(1); // 短暂休眠
            }
            // 线程完成后退出
            QThread::currentThread()->quit();
        });

        thread->start();
    }

    // 等待所有线程完成
    for ( QThread* thread : threads ) {
        if ( !thread->wait(10000) ) {
            thread->terminate();
            thread->wait(1000);
        }
    }

    // 清理worker对象
    for ( QObject* worker : workers ) {
        delete worker;
    }

    // 清理线程对象
    for ( QThread* thread : threads ) {
        delete thread;
    }

    // 验证没有死锁或数据损坏
    qCDebug(lcProducerConsumerTest) << "高并发测试完成，消费数据数量:" << m_consumedCount;
    QVERIFY(m_consumedCount > 0);
}

void TestProducerConsumerIntegration::test_queueStatistics() {
    qCDebug(lcProducerConsumerTest) << "测试队列统计信息";

    // 获取队列统计信息
    QueueStats stats = m_queueManager->getQueueStats(QueueManager::CaptureQueue);

    // 验证初始统计信息
    QVERIFY(stats.currentSize >= 0);
    QVERIFY(stats.maxSize >= 0);
    QVERIFY(stats.totalEnqueued >= 0);
    QVERIFY(stats.totalDequeued >= 0);

    // 添加一些数据
    const int testItems = 5;
    for ( int i = 0; i < testItems; ++i ) {
        QImage image = createTestImage(100, 100, i);
        CapturedFrame frame = createTestFrame(i, image);
        m_queueManager->enqueueCapturedFrame(frame);
    }

    // 强制更新统计信息
    m_queueManager->forceUpdateStats();

    // 获取更新后的统计信息
    QueueStats updatedStats = m_queueManager->getQueueStats(QueueManager::CaptureQueue);

    // 验证统计信息更新
    QCOMPARE(updatedStats.currentSize, testItems);
    QVERIFY(updatedStats.totalEnqueued >= stats.totalEnqueued + testItems);

    // 消费一些数据
    for ( int i = 0; i < 3; ++i ) {
        CapturedFrame frame;
        m_queueManager->dequeueCapturedFrame(frame);
    }

    // 强制更新统计信息
    m_queueManager->forceUpdateStats();

    // 再次获取统计信息
    QueueStats finalStats = m_queueManager->getQueueStats(QueueManager::CaptureQueue);

    // 验证最终统计信息
    QCOMPARE(finalStats.currentSize, testItems - 3);
    QVERIFY(finalStats.totalDequeued >= updatedStats.totalDequeued + 3);
}

QImage TestProducerConsumerIntegration::createTestImage(int width, int height, int pattern) {
    QImage image(width, height, QImage::Format_RGB32);

    // 根据模式填充不同的图像内容
    for ( int y = 0; y < height; ++y ) {
        for ( int x = 0; x < width; ++x ) {
            int r, g, b;
            switch ( pattern % 4 ) {
                case 0:
                    r = (x * 255) / width;
                    g = (y * 255) / height;
                    b = 128;
                    break;
                case 1:
                    r = 255 - (x * 255) / width;
                    g = 128;
                    b = (y * 255) / height;
                    break;
                case 2:
                    r = 128;
                    g = 255 - (y * 255) / height;
                    b = (x * 255) / width;
                    break;
                default:
                    r = (x + y) % 256;
                    g = (x * y) % 256;
                    b = (x ^ y) % 256;
                    break;
            }
            image.setPixel(x, y, qRgb(r, g, b));
        }
    }

    return image;
}

CapturedFrame TestProducerConsumerIntegration::createTestFrame(quint64 frameId, const QImage& image) {
    CapturedFrame frame;
    frame.frameId = frameId;
    frame.timestamp = QDateTime::currentDateTime();
    frame.originalSize = QSize(image.width(), image.height());
    frame.image = image;


    return frame;
}

bool TestProducerConsumerIntegration::waitForQueueProcessing(int maxWaitMs) {
    QElapsedTimer timer;
    timer.start();

    int lastProcessedSize = 0;
    int stableCount = 0;

    while ( timer.elapsed() < maxWaitMs ) {
        auto processedStats = m_queueManager->getQueueStats(QueueManager::ProcessedQueue);
        int currentProcessedSize = processedStats.currentSize;

        // 如果捕获队列为空且处理队列有数据，检查处理队列大小是否稳定
        auto captureStats = m_queueManager->getQueueStats(QueueManager::CaptureQueue);
        if ( captureStats.currentSize == 0 ) {
            if ( currentProcessedSize == lastProcessedSize && currentProcessedSize > 0 ) {
                stableCount++;
                // 如果处理队列大小连续3次检查都稳定，认为处理完成
                if ( stableCount >= 3 ) {
                    return true;
                }
            } else {
                stableCount = 0;
            }
            lastProcessedSize = currentProcessedSize;
        }

        QThread::msleep(50);
        QCoreApplication::processEvents();
    }

    return false;
}

bool TestProducerConsumerIntegration::verifyProcessedData(const ProcessedData& processedData, const CapturedFrame& originalFrame) {
    // 验证基本字段
    if ( processedData.originalFrameId != originalFrame.frameId ) {
        qCWarning(lcProducerConsumerTest) << "帧ID不匹配:" << processedData.originalFrameId << "vs" << originalFrame.frameId;
        return false;
    }

    if ( processedData.imageSize != originalFrame.originalSize ) {
        qCWarning(lcProducerConsumerTest) << "图像尺寸不匹配:" << processedData.imageSize << "vs" << originalFrame.originalSize;
        return false;
    }

    // 验证处理后的图像数据不为空
    if ( processedData.compressedData.isEmpty() ) {
        qCWarning(lcProducerConsumerTest) << "处理后的图像数据为空";
        return false;
    }

    // 验证时间戳
    if ( processedData.processedTime <= originalFrame.timestamp ) {
        qCWarning(lcProducerConsumerTest) << "处理时间戳不合理";
        return false;
    }

    return true;
}

QTEST_MAIN(TestProducerConsumerIntegration)
#include "test_producer_consumer_integration.moc"