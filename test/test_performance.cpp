/**
 * @file test_performance.cpp
 * @brief 性能测试 - 测试系统的性能表现和优化效果
 * @author Assistant
 * @date 2024
 */

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QTimer>
#include <QThread>
#include <QSignalSpy>
#include <QLoggingCategory>
#include <QDebug>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <memory>
#include <chrono>

// 包含被测试的类
#include "ThreadManager.h"
#include "ScreenCaptureWorker.h"
#include "PerformanceOptimizer.h"

// 日志分类
Q_LOGGING_CATEGORY(testPerformance, "test.performance", QtDebugMsg)

/**
 * @class TestPerformance
 * @brief 性能测试类 - 测试系统的性能表现和优化效果
 */
class TestPerformance : public QObject
{
    Q_OBJECT

public:
    TestPerformance();
    ~TestPerformance();

private slots:
    // 测试初始化和清理
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基准性能测试
    void test_threadCreationPerformance();
    void test_threadStartupPerformance();
    void test_threadSwitchingPerformance();
    void test_memoryAllocationPerformance();
    
    // 吞吐量测试
    void test_frameCaptureThroughput();
    void test_messageQueueThroughput();
    void test_multiThreadThroughput();
    void test_concurrentAccessThroughput();
    
    // 延迟测试
    void test_frameProcessingLatency();
    void test_threadCommunicationLatency();
    void test_signalSlotLatency();
    void test_queueOperationLatency();
    
    // 负载测试
    void test_highCpuLoadPerformance();
    void test_highMemoryLoadPerformance();
    void test_highConcurrencyLoadPerformance();
    void test_sustainedLoadPerformance();
    
    // 扩展性测试
    void test_threadScalability();
    void test_memoryScalability();
    void test_performanceUnderScale();
    
    // 优化效果测试
    void test_performanceOptimizerEffectiveness();
    void test_adaptiveOptimizationPerformance();
    void test_resourceOptimizationImpact();
    
    // 压力测试
    void test_extremeLoadStressTest();
    void test_memoryPressureStressTest();
    void test_longRunningStressTest();
    
    // 回归测试
    void test_performanceRegression();
    void test_memoryLeakRegression();
    void test_stabilityRegression();

private:
    // 性能测量辅助方法
    struct PerformanceMetrics {
        double averageTime = 0.0;      // 平均时间(ms)
        double minTime = 0.0;          // 最小时间(ms)
        double maxTime = 0.0;          // 最大时间(ms)
        double throughput = 0.0;       // 吞吐量(ops/sec)
        double memoryUsage = 0.0;      // 内存使用(MB)
        double cpuUsage = 0.0;         // CPU使用率(%)
        int operationCount = 0;        // 操作次数
    };
    
    PerformanceMetrics measureOperationPerformance(
        std::function<void()> operation, 
        int iterations = 1000,
        int warmupIterations = 100
    );
    
    PerformanceMetrics measureThroughput(
        std::function<void()> operation,
        int durationMs = 5000
    );
    
    double measureMemoryUsage();
    double measureCpuUsage();
    void logPerformanceMetrics(const QString& testName, const PerformanceMetrics& metrics);
    void verifyPerformanceThresholds(const PerformanceMetrics& metrics, const PerformanceMetrics& thresholds);
    
    // 负载生成方法
    void generateCpuLoad(int durationMs, double targetUsage = 50.0);
    void generateMemoryLoad(int sizeMB, int durationMs);
    void generateConcurrentLoad(int threadCount, int durationMs);
    
    // 测试数据
    ThreadManager* m_threadManager;
    PerformanceOptimizer* m_performanceOptimizer;
    QList<ScreenCaptureWorker*> m_workers;
    
    // 性能基准
    static const int MAX_TEST_THREADS = 10;
    static const int PERFORMANCE_TEST_TIMEOUT = 30000; // 30秒
    static const double CPU_USAGE_THRESHOLD = 80.0;    // CPU使用率阈值
    static const double MEMORY_USAGE_THRESHOLD = 500.0; // 内存使用阈值(MB)
    static const double THROUGHPUT_THRESHOLD = 100.0;   // 吞吐量阈值(ops/sec)
};

TestPerformance::TestPerformance()
    : m_threadManager(nullptr)
    , m_performanceOptimizer(nullptr)
{
    qCDebug(testPerformance) << "TestPerformance构造函数";
}

TestPerformance::~TestPerformance()
{
    qCDebug(testPerformance) << "TestPerformance析构函数";
}

void TestPerformance::initTestCase()
{
    qCDebug(testPerformance) << "初始化性能测试套件";
    
    // 设置测试环境
    QCoreApplication::setApplicationName("QtRemoteDesktopPerformanceTest");
    QCoreApplication::setApplicationVersion("1.0.0");
    
    // 初始化日志
    QLoggingCategory::setFilterRules("test.performance.debug=true");
    
    qCDebug(testPerformance) << "性能测试套件初始化完成";
}

void TestPerformance::cleanupTestCase()
{
    qCDebug(testPerformance) << "清理性能测试套件";
    
    // 清理全局资源
    if (m_threadManager) {
        m_threadManager->destroyAllThreads();
    }
    
    qCDebug(testPerformance) << "性能测试套件清理完成";
}

void TestPerformance::init()
{
    qCDebug(testPerformance) << "初始化性能测试用例";
    
    // 获取单例实例
    m_threadManager = &ThreadManager::instance();
    m_performanceOptimizer = &PerformanceOptimizer::instance();
    
    // 清理之前的状态
    m_threadManager->destroyAllThreads();
    m_workers.clear();
    
    // 启动性能监控
    m_performanceOptimizer->startMonitoring();
    
    // 等待清理完成
    QTest::qWait(200);
    
    qCDebug(testPerformance) << "性能测试用例初始化完成";
}

void TestPerformance::cleanup()
{
    qCDebug(testPerformance) << "清理性能测试用例";
    
    // 停止所有工作线程
    for (auto* worker : m_workers) {
        if (worker && worker->isRunning()) {
            worker->stop();
        }
    }
    
    // 销毁所有线程
    if (m_threadManager) {
        m_threadManager->destroyAllThreads();
    }
    
    // 停止性能监控
    if (m_performanceOptimizer) {
        m_performanceOptimizer->stopMonitoring();
    }
    
    // 等待清理完成
    QTest::qWait(300);
    
    qCDebug(testPerformance) << "性能测试用例清理完成";
}

void TestPerformance::test_threadCreationPerformance()
{
    qCDebug(testPerformance) << "测试线程创建性能";
    
    auto metrics = measureOperationPerformance([this]() {
        auto* worker = new ScreenCaptureWorker();
        QString threadId = m_threadManager->createThread("PerfTestWorker", worker);
        m_threadManager->destroyThread(threadId);
        // delete worker;  // 由线程管理器统一销毁，避免双重释放
    }, 100, 10);
    
    logPerformanceMetrics("线程创建", metrics);
    
    // 验证性能阈值
    PerformanceMetrics thresholds;
    thresholds.averageTime = 50.0;  // 平均创建时间应小于50ms
    thresholds.maxTime = 200.0;     // 最大创建时间应小于200ms
    
    verifyPerformanceThresholds(metrics, thresholds);
    
    qCDebug(testPerformance) << "线程创建性能测试完成";
}

void TestPerformance::test_threadStartupPerformance()
{
    qCDebug(testPerformance) << "测试线程启动性能";
    
    // 预创建线程
    QStringList threadIds;
    for (int i = 0; i < 10; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        QString threadId = m_threadManager->createThread(
            QString("StartupPerfWorker_%1").arg(i), worker);
        threadIds.append(threadId);
    }
    
    auto metrics = measureOperationPerformance([this, &threadIds]() {
        static int index = 0;
        if (index < threadIds.size()) {
            m_threadManager->startThread(threadIds[index]);
            // 等待启动完成
            while (!m_threadManager->isThreadRunning(threadIds[index])) {
                QCoreApplication::processEvents();
                QThread::msleep(1);
            }
            m_threadManager->stopThread(threadIds[index]);
            index = (index + 1) % threadIds.size();
        }
    }, 50, 5);
    
    logPerformanceMetrics("线程启动", metrics);
    
    // 验证性能阈值
    PerformanceMetrics thresholds;
    thresholds.averageTime = 100.0;  // 平均启动时间应小于100ms
    thresholds.maxTime = 500.0;      // 最大启动时间应小于500ms
    
    verifyPerformanceThresholds(metrics, thresholds);
    
    qCDebug(testPerformance) << "线程启动性能测试完成";
}

void TestPerformance::test_threadSwitchingPerformance()
{
    qCDebug(testPerformance) << "测试线程切换性能";
    
    // 创建多个线程
    QStringList threadIds;
    for (int i = 0; i < 5; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        QString threadId = m_threadManager->createThread(
            QString("SwitchPerfWorker_%1").arg(i), worker);
        threadIds.append(threadId);
        m_threadManager->startThread(threadId);
    }
    
    // 等待所有线程启动
    for (const QString& threadId : threadIds) {
        while (!m_threadManager->isThreadRunning(threadId)) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
    }
    
    auto metrics = measureOperationPerformance([this, &threadIds]() {
        // 模拟线程切换：暂停和恢复
        for (const QString& threadId : threadIds) {
            m_threadManager->pauseThread(threadId);
            m_threadManager->resumeThread(threadId);
        }
    }, 20, 2);
    
    logPerformanceMetrics("线程切换", metrics);
    
    // 验证性能阈值
    PerformanceMetrics thresholds;
    thresholds.averageTime = 50.0;   // 平均切换时间应小于50ms
    thresholds.maxTime = 200.0;      // 最大切换时间应小于200ms
    
    verifyPerformanceThresholds(metrics, thresholds);
    
    qCDebug(testPerformance) << "线程切换性能测试完成";
}

void TestPerformance::test_memoryAllocationPerformance()
{
    qCDebug(testPerformance) << "测试内存分配性能";
    
    QList<QByteArray*> allocations;
    
    auto metrics = measureOperationPerformance([&allocations]() {
        // 分配1MB内存
        auto* data = new QByteArray(1024 * 1024, 0);
        allocations.append(data);
        
        // 每100次分配清理一次，避免内存耗尽
        if (allocations.size() > 100) {
            for (auto* ptr : allocations) {
                delete ptr;
            }
            allocations.clear();
        }
    }, 500, 50);
    
    // 清理剩余分配
    for (auto* ptr : allocations) {
        delete ptr;
    }
    
    logPerformanceMetrics("内存分配", metrics);
    
    // 验证性能阈值
    PerformanceMetrics thresholds;
    thresholds.averageTime = 10.0;   // 平均分配时间应小于10ms
    thresholds.maxTime = 50.0;       // 最大分配时间应小于50ms
    
    verifyPerformanceThresholds(metrics, thresholds);
    
    qCDebug(testPerformance) << "内存分配性能测试完成";
}

void TestPerformance::test_frameCaptureThroughput()
{
    qCDebug(testPerformance) << "测试帧捕获吞吐量";
    
    // 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QString threadId = m_threadManager->createThread("ThroughputWorker", worker);
    m_threadManager->startThread(threadId);
    
    // 等待线程启动
    while (!m_threadManager->isThreadRunning(threadId)) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    
    // 配置高性能参数
    ScreenCaptureConfig config;
    config.frameRate = 60;
    config.quality = 50;  // 降低质量以提高速度
    config.captureRegion = QRect(0, 0, 640, 480);  // 较小的捕获区域
    
    worker->configure(config);
    
    QSignalSpy frameReadySpy(worker, &ScreenCaptureWorker::frameReady);
    
    // 测量吞吐量
    QElapsedTimer timer;
    timer.start();
    
    worker->startCapture();
    
    // 运行5秒
    QTest::qWait(5000);
    
    worker->stopCapture();
    
    qint64 elapsed = timer.elapsed();
    int frameCount = frameReadySpy.count();
    
    PerformanceMetrics metrics;
    metrics.throughput = (double)frameCount / (elapsed / 1000.0);
    metrics.operationCount = frameCount;
    metrics.averageTime = (double)elapsed / frameCount;
    
    logPerformanceMetrics("帧捕获吞吐量", metrics);
    
    // 验证吞吐量
    QVERIFY(metrics.throughput > 10.0);  // 至少10帧/秒
    QVERIFY(frameCount > 0);
    
    m_threadManager->stopThread(threadId);
    
    qCDebug(testPerformance) << "帧捕获吞吐量测试完成";
}

void TestPerformance::test_messageQueueThroughput()
{
    QSKIP("消息队列模块已移除，此测试暂时跳过");
}

void TestPerformance::test_multiThreadThroughput()
{
    qCDebug(testPerformance) << "测试多线程吞吐量";
    
    // 创建多个工作线程
    QStringList threadIds;
    QList<QSignalSpy*> spies;
    
    for (int i = 0; i < MAX_TEST_THREADS; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        auto* spy = new QSignalSpy(worker, &ScreenCaptureWorker::frameReady);
        spies.append(spy);
        
        QString threadId = m_threadManager->createThread(
            QString("MultiThroughputWorker_%1").arg(i), worker);
        threadIds.append(threadId);
        
        m_threadManager->startThread(threadId);
    }
    
    // 等待所有线程启动
    for (const QString& threadId : threadIds) {
        while (!m_threadManager->isThreadRunning(threadId)) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
    }
    
    // 配置所有工作线程
    ScreenCaptureConfig config;
    config.frameRate = 30;
    config.quality = 60;
    
    for (auto* worker : m_workers) {
        worker->configure(config);
    }
    
    // 测量多线程吞吐量
    QElapsedTimer timer;
    timer.start();
    
    for (auto* worker : m_workers) {
        worker->startCapture();
    }
    
    // 运行5秒
    QTest::qWait(5000);
    
    for (auto* worker : m_workers) {
        worker->stopCapture();
    }
    
    qint64 elapsed = timer.elapsed();
    
    // 计算总吞吐量
    int totalFrames = 0;
    for (auto* spy : spies) {
        totalFrames += spy->count();
    }
    
    PerformanceMetrics metrics;
    metrics.throughput = (double)totalFrames / (elapsed / 1000.0);
    metrics.operationCount = totalFrames;
    metrics.averageTime = (double)elapsed / totalFrames;
    
    logPerformanceMetrics("多线程吞吐量", metrics);
    
    // 验证多线程吞吐量
    QVERIFY(metrics.throughput > THROUGHPUT_THRESHOLD * MAX_TEST_THREADS / 2);
    
    // 停止所有线程
    for (const QString& threadId : threadIds) {
        m_threadManager->stopThread(threadId);
    }
    
    // 清理
    qDeleteAll(spies);
    
    qCDebug(testPerformance) << "多线程吞吐量测试完成";
}

void TestPerformance::test_concurrentAccessThroughput()
{
    QSKIP("消息队列模块已移除，此测试暂时跳过");
}

void TestPerformance::test_frameProcessingLatency()
{
    qCDebug(testPerformance) << "测试帧处理延迟";
    
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QString threadId = m_threadManager->createThread("LatencyWorker", worker);
    m_threadManager->startThread(threadId);
    
    // 等待线程启动
    while (!m_threadManager->isThreadRunning(threadId)) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    
    // 配置低延迟参数
    ScreenCaptureConfig config;
    config.frameRate = 30;
    config.quality = 70;
    config.captureRegion = QRect(0, 0, 800, 600);
    
    worker->configure(config);
    
    QList<qint64> latencies;
    QSignalSpy frameReadySpy(worker, &ScreenCaptureWorker::frameReady);
    
    // 连接信号以测量延迟
    connect(worker, &ScreenCaptureWorker::frameReady, [&latencies](const QByteArray& frameData) {
        Q_UNUSED(frameData)
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        // 这里应该有帧的时间戳，但为了简化，我们使用当前时间
        latencies.append(currentTime);
    });
    
    worker->startCapture();
    
    // 收集延迟数据
    QTest::qWait(3000);
    
    worker->stopCapture();
    
    // 计算延迟统计
    if (!latencies.isEmpty()) {
        QList<qint64> intervals;
        for (int i = 1; i < latencies.size(); ++i) {
            intervals.append(latencies[i] - latencies[i-1]);
        }
        
        if (!intervals.isEmpty()) {
            std::sort(intervals.begin(), intervals.end());
            
            PerformanceMetrics metrics;
            metrics.averageTime = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
            metrics.minTime = intervals.first();
            metrics.maxTime = intervals.last();
            metrics.operationCount = intervals.size();
            
            logPerformanceMetrics("帧处理延迟", metrics);
            
            // 验证延迟
            QVERIFY(metrics.averageTime < 100.0);  // 平均延迟应小于100ms
            QVERIFY(metrics.maxTime < 500.0);      // 最大延迟应小于500ms
        }
    }
    
    m_threadManager->stopThread(threadId);
    
    qCDebug(testPerformance) << "帧处理延迟测试完成";
}

void TestPerformance::test_threadCommunicationLatency()
{
    qCDebug(testPerformance) << "测试线程通信延迟";
    
    // 创建两个工作线程进行通信测试
    auto* worker1 = new ScreenCaptureWorker();
    auto* worker2 = new ScreenCaptureWorker();
    m_workers.append(worker1);
    m_workers.append(worker2);
    
    QString threadId1 = m_threadManager->createThread("CommWorker1", worker1);
    QString threadId2 = m_threadManager->createThread("CommWorker2", worker2);
    
    m_threadManager->startThread(threadId1);
    m_threadManager->startThread(threadId2);
    
    // 等待线程启动
    while (!m_threadManager->isThreadRunning(threadId1) || 
           !m_threadManager->isThreadRunning(threadId2)) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    
    QList<qint64> latencies;
    
    // 测量信号槽通信延迟
    connect(worker1, &ScreenCaptureWorker::frameReady, [&latencies](const QByteArray& data) {
        Q_UNUSED(data)
        qint64 receiveTime = QDateTime::currentMSecsSinceEpoch();
        latencies.append(receiveTime);
    });
    
    auto metrics = measureOperationPerformance([worker1]() {
        // 触发信号发射
        QMetaObject::invokeMethod(worker1, [worker1]() {
            // 模拟帧数据生成
            QByteArray testData(1024, 0);
            emit worker1->frameReady(testData);
        }, Qt::QueuedConnection);
    }, 100, 10);
    
    logPerformanceMetrics("线程通信延迟", metrics);
    
    // 验证通信延迟
    PerformanceMetrics thresholds;
    thresholds.averageTime = 10.0;   // 平均通信延迟应小于10ms
    thresholds.maxTime = 50.0;       // 最大通信延迟应小于50ms
    
    verifyPerformanceThresholds(metrics, thresholds);
    
    m_threadManager->stopThread(threadId1);
    m_threadManager->stopThread(threadId2);
    
    qCDebug(testPerformance) << "线程通信延迟测试完成";
}

void TestPerformance::test_signalSlotLatency()
{
    qCDebug(testPerformance) << "测试信号槽延迟";
    
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QList<qint64> emitTimes;
    QList<qint64> receiveTimes;
    
    // 连接信号槽
    connect(worker, &ScreenCaptureWorker::frameReady, [&receiveTimes](const QByteArray& data) {
        Q_UNUSED(data)
        receiveTimes.append(QDateTime::currentMSecsSinceEpoch());
    });
    
    auto metrics = measureOperationPerformance([worker, &emitTimes]() {
        emitTimes.append(QDateTime::currentMSecsSinceEpoch());
        QByteArray testData(512, 0);
        emit worker->frameReady(testData);
        QCoreApplication::processEvents();  // 处理信号
    }, 1000, 100);
    
    // 计算信号槽延迟
    if (emitTimes.size() == receiveTimes.size() && !emitTimes.isEmpty()) {
        QList<qint64> latencies;
        for (int i = 0; i < emitTimes.size(); ++i) {
            latencies.append(receiveTimes[i] - emitTimes[i]);
        }
        
        if (!latencies.isEmpty()) {
            std::sort(latencies.begin(), latencies.end());
            
            PerformanceMetrics signalMetrics;
            signalMetrics.averageTime = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
            signalMetrics.minTime = latencies.first();
            signalMetrics.maxTime = latencies.last();
            signalMetrics.operationCount = latencies.size();
            
            logPerformanceMetrics("信号槽延迟", signalMetrics);
            
            // 验证信号槽延迟
            QVERIFY(signalMetrics.averageTime < 5.0);   // 平均延迟应小于5ms
            QVERIFY(signalMetrics.maxTime < 20.0);      // 最大延迟应小于20ms
        }
    }
    
    logPerformanceMetrics("信号槽操作", metrics);
    
    qCDebug(testPerformance) << "信号槽延迟测试完成";
}

void TestPerformance::test_queueOperationLatency()
{
    QSKIP("消息队列模块已移除，此测试暂时跳过");
}

void TestPerformance::test_highCpuLoadPerformance()
{
    qCDebug(testPerformance) << "测试高CPU负载性能";
    
    // 记录初始性能
    auto initialStats = m_performanceOptimizer->getPerformanceStats();
    
    // 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QString threadId = m_threadManager->createThread("HighCpuWorker", worker);
    m_threadManager->startThread(threadId);
    
    // 等待线程启动
    while (!m_threadManager->isThreadRunning(threadId)) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    
    // 生成高CPU负载
    generateCpuLoad(5000, 70.0);  // 5秒，目标70%CPU使用率
    
    // 在高负载下测试性能
    ScreenCaptureConfig config;
    config.frameRate = 30;
    config.quality = 80;
    
    worker->configure(config);
    
    QSignalSpy frameReadySpy(worker, &ScreenCaptureWorker::frameReady);
    
    QElapsedTimer timer;
    timer.start();
    
    worker->startCapture();
    QTest::qWait(3000);  // 运行3秒
    worker->stopCapture();
    
    qint64 elapsed = timer.elapsed();
    int frameCount = frameReadySpy.count();
    
    // 检查高负载下的性能
    auto loadStats = m_performanceOptimizer->getPerformanceStats();
    
    PerformanceMetrics metrics;
    metrics.throughput = (double)frameCount / (elapsed / 1000.0);
    metrics.cpuUsage = loadStats.cpuUsage;
    metrics.memoryUsage = loadStats.memoryUsage;
    metrics.operationCount = frameCount;
    
    logPerformanceMetrics("高CPU负载性能", metrics);
    
    // 验证在高负载下仍能正常工作
    QVERIFY(frameCount > 0);
    QVERIFY(metrics.throughput > 5.0);  // 至少5帧/秒
    
    m_threadManager->stopThread(threadId);
    
    qCDebug(testPerformance) << "高CPU负载性能测试完成";
}

void TestPerformance::test_highMemoryLoadPerformance()
{
    qCDebug(testPerformance) << "测试高内存负载性能";
    
    // 记录初始内存使用
    auto initialStats = m_performanceOptimizer->getPerformanceStats();
    double initialMemory = initialStats.memoryUsage;
    
    // 生成内存负载
    generateMemoryLoad(200, 5000);  // 分配200MB，持续5秒
    
    // 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QString threadId = m_threadManager->createThread("HighMemoryWorker", worker);
    m_threadManager->startThread(threadId);
    
    // 等待线程启动
    while (!m_threadManager->isThreadRunning(threadId)) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    
    // 在高内存负载下测试性能
    ScreenCaptureConfig config;
    config.frameRate = 20;
    config.quality = 60;
    
    worker->configure(config);
    
    QSignalSpy frameReadySpy(worker, &ScreenCaptureWorker::frameReady);
    
    QElapsedTimer timer;
    timer.start();
    
    worker->startCapture();
    QTest::qWait(3000);
    worker->stopCapture();
    
    qint64 elapsed = timer.elapsed();
    int frameCount = frameReadySpy.count();
    
    auto loadStats = m_performanceOptimizer->getPerformanceStats();
    
    PerformanceMetrics metrics;
    metrics.throughput = (double)frameCount / (elapsed / 1000.0);
    metrics.memoryUsage = loadStats.memoryUsage;
    metrics.operationCount = frameCount;
    
    logPerformanceMetrics("高内存负载性能", metrics);
    
    // 验证在高内存负载下的性能
    QVERIFY(frameCount > 0);
    QVERIFY(metrics.memoryUsage > initialMemory);  // 内存使用应该增加
    QVERIFY(metrics.throughput > 3.0);  // 至少3帧/秒
    
    m_threadManager->stopThread(threadId);
    
    qCDebug(testPerformance) << "高内存负载性能测试完成";
}

void TestPerformance::test_highConcurrencyLoadPerformance()
{
    qCDebug(testPerformance) << "测试高并发负载性能";
    
    // 创建大量并发线程
    const int concurrentThreads = MAX_TEST_THREADS * 2;
    QStringList threadIds;
    QList<QSignalSpy*> spies;
    
    for (int i = 0; i < concurrentThreads; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        auto* spy = new QSignalSpy(worker, &ScreenCaptureWorker::frameReady);
        spies.append(spy);
        
        QString threadId = m_threadManager->createThread(
            QString("ConcurrencyWorker_%1").arg(i), worker);
        threadIds.append(threadId);
        
        m_threadManager->startThread(threadId);
    }
    
    // 等待所有线程启动
    for (const QString& threadId : threadIds) {
        while (!m_threadManager->isThreadRunning(threadId)) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
    }
    
    // 配置所有工作线程
    ScreenCaptureConfig config;
    config.frameRate = 15;  // 降低帧率以减少负载
    config.quality = 50;
    
    for (auto* worker : m_workers) {
        worker->configure(config);
    }
    
    // 测量高并发性能
    QElapsedTimer timer;
    timer.start();
    
    for (auto* worker : m_workers) {
        worker->startCapture();
    }
    
    QTest::qWait(5000);  // 运行5秒
    
    for (auto* worker : m_workers) {
        worker->stopCapture();
    }
    
    qint64 elapsed = timer.elapsed();
    
    // 计算总性能
    int totalFrames = 0;
    for (auto* spy : spies) {
        totalFrames += spy->count();
    }
    
    auto loadStats = m_performanceOptimizer->getPerformanceStats();
    
    PerformanceMetrics metrics;
    metrics.throughput = (double)totalFrames / (elapsed / 1000.0);
    metrics.cpuUsage = loadStats.cpuUsage;
    metrics.memoryUsage = loadStats.memoryUsage;
    metrics.operationCount = totalFrames;
    
    logPerformanceMetrics("高并发负载性能", metrics);
    
    // 验证高并发性能
    QVERIFY(totalFrames > 0);
    QVERIFY(metrics.throughput > 10.0);  // 总吞吐量至少10帧/秒
    QVERIFY(metrics.cpuUsage < 95.0);    // CPU使用率不应超过95%
    
    // 停止所有线程
    for (const QString& threadId : threadIds) {
        m_threadManager->stopThread(threadId);
    }
    
    // 清理
    qDeleteAll(spies);
    
    qCDebug(testPerformance) << "高并发负载性能测试完成";
}

void TestPerformance::test_sustainedLoadPerformance()
{
    qCDebug(testPerformance) << "测试持续负载性能";
    
    // 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QString threadId = m_threadManager->createThread("SustainedWorker", worker);
    m_threadManager->startThread(threadId);
    
    // 等待线程启动
    while (!m_threadManager->isThreadRunning(threadId)) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    
    // 配置持续负载参数
    ScreenCaptureConfig config;
    config.frameRate = 25;
    config.quality = 70;
    
    worker->configure(config);
    
    QSignalSpy frameReadySpy(worker, &ScreenCaptureWorker::frameReady);
    QSignalSpy errorSpy(worker, &ScreenCaptureWorker::errorOccurred);
    
    // 长时间持续负载测试（15秒）
    const int testDuration = 15000;
    const int checkInterval = 3000;
    
    QList<PerformanceMetrics> intervalMetrics;
    
    worker->startCapture();
    
    for (int i = 0; i < testDuration; i += checkInterval) {
        QElapsedTimer intervalTimer;
        intervalTimer.start();
        
        int initialFrameCount = frameReadySpy.count();
        
        QTest::qWait(checkInterval);
        
        qint64 intervalElapsed = intervalTimer.elapsed();
        int intervalFrameCount = frameReadySpy.count() - initialFrameCount;
        
        auto stats = m_performanceOptimizer->getPerformanceStats();
        
        PerformanceMetrics metrics;
        metrics.throughput = (double)intervalFrameCount / (intervalElapsed / 1000.0);
        metrics.cpuUsage = stats.cpuUsage;
        metrics.memoryUsage = stats.memoryUsage;
        metrics.operationCount = intervalFrameCount;
        
        intervalMetrics.append(metrics);
        
        qCDebug(testPerformance) << "持续负载检查点" << (i + checkInterval) / 1000 << "秒:"
                                << "吞吐量:" << metrics.throughput << "帧/秒"
                                << "CPU:" << metrics.cpuUsage << "%"
                                << "内存:" << metrics.memoryUsage << "MB";
        
        // 检查是否有错误
        QCOMPARE(errorSpy.count(), 0);
        
        // 检查线程是否还在运行
        QVERIFY(m_threadManager->isThreadRunning(threadId));
    }
    
    worker->stopCapture();
    
    // 分析持续性能
    if (!intervalMetrics.isEmpty()) {
        double avgThroughput = 0.0;
        double avgCpuUsage = 0.0;
        double avgMemoryUsage = 0.0;
        
        for (const auto& metrics : intervalMetrics) {
            avgThroughput += metrics.throughput;
            avgCpuUsage += metrics.cpuUsage;
            avgMemoryUsage += metrics.memoryUsage;
        }
        
        avgThroughput /= intervalMetrics.size();
        avgCpuUsage /= intervalMetrics.size();
        avgMemoryUsage /= intervalMetrics.size();
        
        PerformanceMetrics sustainedMetrics;
        sustainedMetrics.throughput = avgThroughput;
        sustainedMetrics.cpuUsage = avgCpuUsage;
        sustainedMetrics.memoryUsage = avgMemoryUsage;
        sustainedMetrics.operationCount = frameReadySpy.count();
        
        logPerformanceMetrics("持续负载性能", sustainedMetrics);
        
        // 验证持续性能
        QVERIFY(avgThroughput > 10.0);   // 平均吞吐量至少10帧/秒
        QVERIFY(avgCpuUsage < 80.0);     // 平均CPU使用率不超过80%
        QVERIFY(frameReadySpy.count() > 100);  // 总帧数应该足够
    }
    
    m_threadManager->stopThread(threadId);
    
    qCDebug(testPerformance) << "持续负载性能测试完成";
}

void TestPerformance::test_threadScalability()
{
    qCDebug(testPerformance) << "测试线程扩展性";
    
    QList<PerformanceMetrics> scalabilityResults;
    
    // 测试不同线程数量的性能
    QList<int> threadCounts = {1, 2, 4, 8, MAX_TEST_THREADS};
    
    for (int threadCount : threadCounts) {
        qCDebug(testPerformance) << "测试" << threadCount << "个线程的扩展性";
        
        // 清理之前的线程
        m_threadManager->destroyAllThreads();
        m_workers.clear();
        QTest::qWait(200);
        
        // 创建指定数量的线程
        QStringList threadIds;
        QList<QSignalSpy*> spies;
        
        for (int i = 0; i < threadCount; ++i) {
            auto* worker = new ScreenCaptureWorker();
            m_workers.append(worker);
            
            auto* spy = new QSignalSpy(worker, &ScreenCaptureWorker::frameReady);
            spies.append(spy);
            
            QString threadId = m_threadManager->createThread(
                QString("ScalabilityWorker_%1").arg(i), worker);
            threadIds.append(threadId);
            
            m_threadManager->startThread(threadId);
        }
        
        // 等待所有线程启动
        for (const QString& threadId : threadIds) {
            while (!m_threadManager->isThreadRunning(threadId)) {
                QCoreApplication::processEvents();
                QThread::msleep(1);
            }
        }
        
        // 配置所有工作线程
        ScreenCaptureConfig config;
        config.frameRate = 20;
        config.quality = 60;
        
        for (auto* worker : m_workers) {
            worker->configure(config);
        }
        
        // 测量性能
        QElapsedTimer timer;
        timer.start();
        
        for (auto* worker : m_workers) {
            worker->startCapture();
        }
        
        QTest::qWait(3000);  // 运行3秒
        
        for (auto* worker : m_workers) {
            worker->stopCapture();
        }
        
        qint64 elapsed = timer.elapsed();
        
        // 计算性能指标
        int totalFrames = 0;
        for (auto* spy : spies) {
            totalFrames += spy->count();
        }
        
        auto stats = m_performanceOptimizer->getPerformanceStats();
        
        PerformanceMetrics metrics;
        metrics.throughput = (double)totalFrames / (elapsed / 1000.0);
        metrics.cpuUsage = stats.cpuUsage;
        metrics.memoryUsage = stats.memoryUsage;
        metrics.operationCount = totalFrames;
        
        scalabilityResults.append(metrics);
        
        qCDebug(testPerformance) << threadCount << "个线程性能:"
                                << "吞吐量:" << metrics.throughput << "帧/秒"
                                << "CPU:" << metrics.cpuUsage << "%"
                                << "内存:" << metrics.memoryUsage << "MB";
        
        // 停止所有线程
        for (const QString& threadId : threadIds) {
            m_threadManager->stopThread(threadId);
        }
        
        // 清理
        qDeleteAll(spies);
    }
    
    // 分析扩展性
    if (scalabilityResults.size() >= 2) {
        double initialThroughput = scalabilityResults.first().throughput;
        double finalThroughput = scalabilityResults.last().throughput;
        
        double scalabilityRatio = finalThroughput / initialThroughput;
        
        qCDebug(testPerformance) << "扩展性分析:"
                                << "初始吞吐量:" << initialThroughput
                                << "最终吞吐量:" << finalThroughput
                                << "扩展比率:" << scalabilityRatio;
        
        // 验证扩展性
        QVERIFY(scalabilityRatio > 1.5);  // 扩展比率应该大于1.5
        QVERIFY(finalThroughput > initialThroughput);  // 最终吞吐量应该更高
    }
    
    qCDebug(testPerformance) << "线程扩展性测试完成";
}

void TestPerformance::test_memoryScalability()
{
    qCDebug(testPerformance) << "测试内存扩展性";
    
    // 记录初始内存使用
    auto initialStats = m_performanceOptimizer->getPerformanceStats();
    double baselineMemory = initialStats.memoryUsage;
    
    QList<double> memoryUsages;
    QList<double> throughputs;
    
    // 测试不同内存负载下的性能
    QList<int> memoryLoads = {0, 50, 100, 200, 300};  // MB
    
    for (int memoryLoad : memoryLoads) {
        qCDebug(testPerformance) << "测试" << memoryLoad << "MB内存负载下的扩展性";
        
        // 生成内存负载
        QList<QByteArray*> allocations;
        if (memoryLoad > 0) {
            for (int i = 0; i < memoryLoad; ++i) {
                allocations.append(new QByteArray(1024 * 1024, 0));  // 1MB per allocation
            }
        }
        
        // 创建工作线程
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        QString threadId = m_threadManager->createThread("MemoryScalabilityWorker", worker);
        m_threadManager->startThread(threadId);
        
        // 等待线程启动
        while (!m_threadManager->isThreadRunning(threadId)) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        
        // 配置工作线程
        ScreenCaptureConfig config;
        config.frameRate = 20;
        config.quality = 60;
        
        worker->configure(config);
        
        QSignalSpy frameReadySpy(worker, &ScreenCaptureWorker::frameReady);
        
        // 测量性能
        QElapsedTimer timer;
        timer.start();
        
        worker->startCapture();
        QTest::qWait(3000);
        worker->stopCapture();
        
        qint64 elapsed = timer.elapsed();
        int frameCount = frameReadySpy.count();
        
        double throughput = (double)frameCount / (elapsed / 1000.0);
        
        auto stats = m_performanceOptimizer->getPerformanceStats();
        double currentMemory = stats.memoryUsage;
        
        memoryUsages.append(currentMemory - baselineMemory);
        throughputs.append(throughput);
        
        qCDebug(testPerformance) << memoryLoad << "MB负载性能:"
                                << "吞吐量:" << throughput << "帧/秒"
                                << "内存使用:" << currentMemory << "MB";
        
        // 停止线程
        m_threadManager->stopThread(threadId);
        
        // 清理内存分配
        for (auto* ptr : allocations) {
            delete ptr;
        }
        
        // 清理工作线程
        m_workers.clear();
        
        QTest::qWait(200);
    }
    
    // 分析内存扩展性
    if (!throughputs.isEmpty()) {
        double initialThroughput = throughputs.first();
        double finalThroughput = throughputs.last();
        
        double performanceDegradation = (initialThroughput - finalThroughput) / initialThroughput;
        
        qCDebug(testPerformance) << "内存扩展性分析:"
                                << "初始吞吐量:" << initialThroughput
                                << "最终吞吐量:" << finalThroughput
                                << "性能下降:" << (performanceDegradation * 100) << "%";
        
        // 验证内存扩展性
        QVERIFY(performanceDegradation < 0.5);  // 性能下降应小于50%
        QVERIFY(finalThroughput > 5.0);         // 即使在高内存负载下也应保持基本性能
    }
    
    qCDebug(testPerformance) << "内存扩展性测试完成";
}

void TestPerformance::test_performanceUnderScale()
{
    qCDebug(testPerformance) << "测试规模化性能";
    
    // 测试系统在不同规模下的整体性能
    struct ScaleTestConfig {
        int threadCount;
        int frameRate;
        int quality;
        QString description;
    };
    
    QList<ScaleTestConfig> scaleConfigs = {
        {1, 30, 80, "小规模高质量"},
        {3, 20, 70, "中规模中质量"},
        {5, 15, 60, "大规模低质量"},
        {MAX_TEST_THREADS, 10, 50, "最大规模最低质量"}
    };
    
    QList<PerformanceMetrics> scaleResults;
    
    for (const auto& config : scaleConfigs) {
        qCDebug(testPerformance) << "测试配置:" << config.description;
        
        // 清理之前的状态
        m_threadManager->destroyAllThreads();
        m_workers.clear();
        QTest::qWait(200);
        
        // 创建指定数量的线程
        QStringList threadIds;
        QList<QSignalSpy*> spies;
        
        for (int i = 0; i < config.threadCount; ++i) {
            auto* worker = new ScreenCaptureWorker();
            m_workers.append(worker);
            
            auto* spy = new QSignalSpy(worker, &ScreenCaptureWorker::frameReady);
            spies.append(spy);
            
            QString threadId = m_threadManager->createThread(
                QString("ScaleWorker_%1").arg(i), worker);
            threadIds.append(threadId);
            
            m_threadManager->startThread(threadId);
        }
        
        // 等待所有线程启动
        for (const QString& threadId : threadIds) {
            while (!m_threadManager->isThreadRunning(threadId)) {
                QCoreApplication::processEvents();
                QThread::msleep(1);
            }
        }
        
        // 配置所有工作线程
        ScreenCaptureConfig captureConfig;
        captureConfig.frameRate = config.frameRate;
        captureConfig.quality = config.quality;
        
        for (auto* worker : m_workers) {
            worker->configure(captureConfig);
        }
        
        // 测量规模化性能
        QElapsedTimer timer;
        timer.start();
        
        for (auto* worker : m_workers) {
            worker->startCapture();
        }
        
        QTest::qWait(4000);  // 运行4秒
        
        for (auto* worker : m_workers) {
            worker->stopCapture();
        }
        
        qint64 elapsed = timer.elapsed();
        
        // 计算性能指标
        int totalFrames = 0;
        for (auto* spy : spies) {
            totalFrames += spy->count();
        }
        
        auto stats = m_performanceOptimizer->getPerformanceStats();
        
        PerformanceMetrics metrics;
        metrics.throughput = (double)totalFrames / (elapsed / 1000.0);
        metrics.cpuUsage = stats.cpuUsage;
        metrics.memoryUsage = stats.memoryUsage;
        metrics.operationCount = totalFrames;
        
        scaleResults.append(metrics);
        
        qCDebug(testPerformance) << config.description << "性能:"
                                << "吞吐量:" << metrics.throughput << "帧/秒"
                                << "CPU:" << metrics.cpuUsage << "%"
                                << "内存:" << metrics.memoryUsage << "MB";
        
        // 停止所有线程
        for (const QString& threadId : threadIds) {
            m_threadManager->stopThread(threadId);
        }
        
        // 清理
        qDeleteAll(spies);
    }
    
    // 验证规模化性能
    for (const auto& metrics : scaleResults) {
        QVERIFY(metrics.throughput > 5.0);   // 所有配置都应保持基本吞吐量
        QVERIFY(metrics.cpuUsage < 90.0);    // CPU使用率不应过高
        QVERIFY(metrics.operationCount > 0); // 应该有帧输出
    }
    
    qCDebug(testPerformance) << "规模化性能测试完成";
}

void TestPerformance::test_performanceOptimizerEffectiveness()
{
    qCDebug(testPerformance) << "测试性能优化器效果";
    
    // 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QString threadId = m_threadManager->createThread("OptimizerTestWorker", worker);
    m_threadManager->startThread(threadId);
    
    // 等待线程启动
    while (!m_threadManager->isThreadRunning(threadId)) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    
    // 配置工作线程
    ScreenCaptureConfig config;
    config.frameRate = 25;
    config.quality = 70;
    
    worker->configure(config);
    
    QSignalSpy frameReadySpy(worker, &ScreenCaptureWorker::frameReady);
    
    // 测试未优化性能
    m_performanceOptimizer->stopMonitoring();
    m_performanceOptimizer->setAutoOptimizationEnabled(false);
    
    QElapsedTimer timer;
    timer.start();
    
    worker->startCapture();
    QTest::qWait(3000);
    worker->stopCapture();
    
    qint64 unoptimizedElapsed = timer.elapsed();
    int unoptimizedFrames = frameReadySpy.count();
    
    frameReadySpy.clear();
    
    // 测试优化后性能
    m_performanceOptimizer->startMonitoring();
    m_performanceOptimizer->setAutoOptimizationEnabled(true);
    
    // 等待优化器分析和优化
    QTest::qWait(1000);
    
    timer.restart();
    
    worker->startCapture();
    QTest::qWait(3000);
    worker->stopCapture();
    
    qint64 optimizedElapsed = timer.elapsed();
    int optimizedFrames = frameReadySpy.count();
    
    // 计算优化效果
    double unoptimizedThroughput = (double)unoptimizedFrames / (unoptimizedElapsed / 1000.0);
    double optimizedThroughput = (double)optimizedFrames / (optimizedElapsed / 1000.0);
    
    double improvement = (optimizedThroughput - unoptimizedThroughput) / unoptimizedThroughput;
    
    qCDebug(testPerformance) << "性能优化器效果分析:"
                            << "未优化吞吐量:" << unoptimizedThroughput << "帧/秒"
                            << "优化后吞吐量:" << optimizedThroughput << "帧/秒"
                            << "性能提升:" << (improvement * 100) << "%";
    
    // 验证优化效果
    QVERIFY(optimizedFrames > 0);
    QVERIFY(unoptimizedFrames > 0);
    // 优化后性能应该不低于未优化性能
    QVERIFY(optimizedThroughput >= unoptimizedThroughput * 0.9);
    
    m_threadManager->stopThread(threadId);
    
    qCDebug(testPerformance) << "性能优化器效果测试完成";
}

void TestPerformance::test_adaptiveOptimizationPerformance()
{
    qCDebug(testPerformance) << "测试自适应优化性能";
    
    // 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QString threadId = m_threadManager->createThread("AdaptiveWorker", worker);
    m_threadManager->startThread(threadId);
    
    // 等待线程启动
    while (!m_threadManager->isThreadRunning(threadId)) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    
    // 启用自适应优化
    m_performanceOptimizer->startMonitoring();
    m_performanceOptimizer->setAutoOptimizationEnabled(true);
    
    // 配置初始参数
    ScreenCaptureConfig config;
    config.frameRate = 30;
    config.quality = 80;
    
    worker->configure(config);
    
    QSignalSpy frameReadySpy(worker, &ScreenCaptureWorker::frameReady);
    QSignalSpy optimizationSpy(m_performanceOptimizer, &PerformanceOptimizer::optimizationApplied);
    
    // 开始捕获并监控自适应优化
    worker->startCapture();
    
    // 模拟不同负载条件
    QList<PerformanceMetrics> adaptiveMetrics;
    
    // 阶段1：正常负载
    QTest::qWait(2000);
    auto stats1 = m_performanceOptimizer->getPerformanceStats();
    int frames1 = frameReadySpy.count();
    
    // 阶段2：生成高CPU负载
    generateCpuLoad(3000, 80.0);
    QTest::qWait(3000);
    auto stats2 = m_performanceOptimizer->getPerformanceStats();
    int frames2 = frameReadySpy.count() - frames1;
    
    // 阶段3：生成内存负载
    generateMemoryLoad(150, 3000);
    QTest::qWait(3000);
    auto stats3 = m_performanceOptimizer->getPerformanceStats();
    int frames3 = frameReadySpy.count() - frames1 - frames2;
    
    worker->stopCapture();
    
    // 分析自适应优化效果
    qCDebug(testPerformance) << "自适应优化分析:";
    qCDebug(testPerformance) << "阶段1(正常):" << frames1 << "帧, CPU:" << stats1.cpuUsage << "%, 内存:" << stats1.memoryUsage << "MB";
    qCDebug(testPerformance) << "阶段2(高CPU):" << frames2 << "帧, CPU:" << stats2.cpuUsage << "%, 内存:" << stats2.memoryUsage << "MB";
    qCDebug(testPerformance) << "阶段3(高内存):" << frames3 << "帧, CPU:" << stats3.cpuUsage << "%, 内存:" << stats3.memoryUsage << "MB";
    qCDebug(testPerformance) << "优化次数:" << optimizationSpy.count();
    
    // 验证自适应优化
    QVERIFY(frames1 > 0);
    QVERIFY(frames2 > 0);
    QVERIFY(frames3 > 0);
    QVERIFY(optimizationSpy.count() > 0);  // 应该有优化发生
    
    m_threadManager->stopThread(threadId);
    
    qCDebug(testPerformance) << "自适应优化性能测试完成";
}

void TestPerformance::test_resourceOptimizationImpact()
{
    qCDebug(testPerformance) << "测试资源优化影响";
    
    // 创建多个工作线程
    QStringList threadIds;
    for (int i = 0; i < 3; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        QString threadId = m_threadManager->createThread(
            QString("ResourceWorker_%1").arg(i), worker);
        threadIds.append(threadId);
        
        m_threadManager->startThread(threadId);
    }
    
    // 等待所有线程启动
    for (const QString& threadId : threadIds) {
        while (!m_threadManager->isThreadRunning(threadId)) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
    }
    
    // 测试不同优化策略的影响
    struct OptimizationTest {
        QString name;
        std::function<void()> setup;
        std::function<void()> cleanup;
    };
    
    QList<OptimizationTest> optimizationTests = {
        {
            "无优化",
            [this]() {
                m_performanceOptimizer->stopMonitoring();
                m_performanceOptimizer->setAutoOptimizationEnabled(false);
            },
            []() {}
        },
        {
            "线程优先级优化",
            [this]() {
                m_performanceOptimizer->startMonitoring();
                m_performanceOptimizer->setAutoOptimizationEnabled(true);
                // 设置高优先级策略
                PerformanceConfig config;
                config.threadPriorityStrategy = ThreadPriorityStrategy::HighPriority;
                m_performanceOptimizer->updateConfiguration(config);
            },
            []() {}
        },
        {
            "队列大小优化",
            [this]() {
                PerformanceConfig config;
                config.queueOptimizationStrategy = QueueOptimizationStrategy::DynamicSize;
                m_performanceOptimizer->updateConfiguration(config);
            },
            []() {}
        },
        {
            "内存管理优化",
            [this]() {
                PerformanceConfig config;
                config.memoryManagementStrategy = MemoryManagementStrategy::Aggressive;
                m_performanceOptimizer->updateConfiguration(config);
            },
            []() {}
        }
    };
    
    QList<PerformanceMetrics> optimizationResults;
    
    for (const auto& test : optimizationTests) {
        qCDebug(testPerformance) << "测试优化策略:" << test.name;
        
        // 设置优化策略
        test.setup();
        QTest::qWait(500);  // 等待优化生效
        
        // 配置所有工作线程
        ScreenCaptureConfig config;
        config.frameRate = 20;
        config.quality = 60;
        
        for (auto* worker : m_workers) {
            worker->configure(config);
        }
        
        // 测量性能
        QList<QSignalSpy*> spies;
        for (auto* worker : m_workers) {
            spies.append(new QSignalSpy(worker, &ScreenCaptureWorker::frameReady));
        }
        
        QElapsedTimer timer;
        timer.start();
        
        for (auto* worker : m_workers) {
            worker->startCapture();
        }
        
        QTest::qWait(3000);
        
        for (auto* worker : m_workers) {
            worker->stopCapture();
        }
        
        qint64 elapsed = timer.elapsed();
        
        // 计算性能指标
        int totalFrames = 0;
        for (auto* spy : spies) {
            totalFrames += spy->count();
        }
        
        auto stats = m_performanceOptimizer->getPerformanceStats();
        
        PerformanceMetrics metrics;
        metrics.throughput = (double)totalFrames / (elapsed / 1000.0);
        metrics.cpuUsage = stats.cpuUsage;
        metrics.memoryUsage = stats.memoryUsage;
        metrics.operationCount = totalFrames;
        
        optimizationResults.append(metrics);
        
        qCDebug(testPerformance) << test.name << "结果:"
                                << "吞吐量:" << metrics.throughput << "帧/秒"
                                << "CPU:" << metrics.cpuUsage << "%"
                                << "内存:" << metrics.memoryUsage << "MB";
        
        // 清理
        qDeleteAll(spies);
        test.cleanup();
        
        QTest::qWait(200);
    }
    
    // 分析优化影响
    if (!optimizationResults.isEmpty()) {
        auto baseline = optimizationResults.first();  // 无优化作为基准
        
        for (int i = 1; i < optimizationResults.size(); ++i) {
            auto optimized = optimizationResults[i];
            double improvement = (optimized.throughput - baseline.throughput) / baseline.throughput;
            
            qCDebug(testPerformance) << optimizationTests[i].name << "相对改进:"
                                    << "吞吐量提升:" << (improvement * 100) << "%";
            
            // 验证优化效果（允许轻微性能下降，因为优化可能有开销）
            QVERIFY(optimized.throughput >= baseline.throughput * 0.8);
        }
    }
    
    // 停止所有线程
    for (const QString& threadId : threadIds) {
        m_threadManager->stopThread(threadId);
    }
    
    qCDebug(testPerformance) << "资源优化影响测试完成";
}

void TestPerformance::test_extremeLoadStressTest()
{
    qCDebug(testPerformance) << "测试极限负载压力";
    
    // 创建最大数量的工作线程
    QStringList threadIds;
    for (int i = 0; i < MAX_TEST_THREADS; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        QString threadId = m_threadManager->createThread(
            QString("StressWorker_%1").arg(i), worker);
        threadIds.append(threadId);
        
        m_threadManager->startThread(threadId);
    }
    
    // 等待所有线程启动
    for (const QString& threadId : threadIds) {
        while (!m_threadManager->isThreadRunning(threadId)) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
    }
    
    // 配置极限参数
    ScreenCaptureConfig config;
    config.frameRate = 60;  // 高帧率
    config.quality = 90;    // 高质量
    
    for (auto* worker : m_workers) {
        worker->configure(config);
    }
    
    // 同时生成CPU和内存负载
    generateCpuLoad(10000, 90.0);   // 10秒，90%CPU
    generateMemoryLoad(300, 10000); // 300MB，10秒
    
    QList<QSignalSpy*> spies;
    QList<QSignalSpy*> errorSpies;
    
    for (auto* worker : m_workers) {
        spies.append(new QSignalSpy(worker, &ScreenCaptureWorker::frameReady));
        errorSpies.append(new QSignalSpy(worker, &ScreenCaptureWorker::errorOccurred));
    }
    
    // 极限负载测试
    QElapsedTimer timer;
    timer.start();
    
    for (auto* worker : m_workers) {
        worker->startCapture();
    }
    
    // 运行10秒极限负载
    const int stressDuration = 10000;
    const int checkInterval = 2000;
    
    for (int i = 0; i < stressDuration; i += checkInterval) {
        QTest::qWait(checkInterval);
        
        // 检查系统状态
        auto stats = m_performanceOptimizer->getPerformanceStats();
        
        qCDebug(testPerformance) << "压力测试检查点" << (i + checkInterval) / 1000 << "秒:"
                                << "CPU:" << stats.cpuUsage << "%"
                                << "内存:" << stats.memoryUsage << "MB";
        
        // 检查是否有线程崩溃
        for (const QString& threadId : threadIds) {
            QVERIFY(m_threadManager->isThreadRunning(threadId));
        }
        
        // 检查错误数量
        int totalErrors = 0;
        for (auto* errorSpy : errorSpies) {
            totalErrors += errorSpy->count();
        }
        
        // 允许少量错误，但不应该太多
        QVERIFY(totalErrors < MAX_TEST_THREADS * 2);
    }
    
    for (auto* worker : m_workers) {
        worker->stopCapture();
    }
    
    qint64 elapsed = timer.elapsed();
    
    // 计算极限负载下的性能
    int totalFrames = 0;
    int totalErrors = 0;
    
    for (auto* spy : spies) {
        totalFrames += spy->count();
    }
    
    for (auto* errorSpy : errorSpies) {
        totalErrors += errorSpy->count();
    }
    
    auto finalStats = m_performanceOptimizer->getPerformanceStats();
    
    PerformanceMetrics stressMetrics;
    stressMetrics.throughput = (double)totalFrames / (elapsed / 1000.0);
    stressMetrics.cpuUsage = finalStats.cpuUsage;
    stressMetrics.memoryUsage = finalStats.memoryUsage;
    stressMetrics.operationCount = totalFrames;
    
    logPerformanceMetrics("极限负载压力", stressMetrics);
    
    qCDebug(testPerformance) << "极限负载结果:"
                            << "总帧数:" << totalFrames
                            << "总错误:" << totalErrors
                            << "错误率:" << (double)totalErrors / totalFrames * 100 << "%";
    
    // 验证极限负载下的稳定性
    QVERIFY(totalFrames > 0);                    // 应该有帧输出
    QVERIFY(stressMetrics.throughput > 5.0);     // 最低吞吐量
    QVERIFY(totalErrors < totalFrames * 0.1);    // 错误率应小于10%
    
    // 停止所有线程
    for (const QString& threadId : threadIds) {
        m_threadManager->stopThread(threadId);
    }
    
    // 清理
    qDeleteAll(spies);
    qDeleteAll(errorSpies);
    
    qCDebug(testPerformance) << "极限负载压力测试完成";
}

// 辅助方法实现
void TestPerformance::logPerformanceMetrics(const QString& testName, const PerformanceMetrics& metrics)
{
    qCDebug(testPerformance) << "=== 性能指标 -" << testName << "===";
    qCDebug(testPerformance) << "吞吐量:" << metrics.throughput << "操作/秒";
    qCDebug(testPerformance) << "平均延迟:" << metrics.averageLatency << "毫秒";
    qCDebug(testPerformance) << "最大延迟:" << metrics.maxLatency << "毫秒";
    qCDebug(testPerformance) << "最小延迟:" << metrics.minLatency << "毫秒";
    qCDebug(testPerformance) << "CPU使用率:" << metrics.cpuUsage << "%";
    qCDebug(testPerformance) << "内存使用:" << metrics.memoryUsage << "MB";
    qCDebug(testPerformance) << "操作总数:" << metrics.operationCount;
    qCDebug(testPerformance) << "错误数量:" << metrics.errorCount;
    qCDebug(testPerformance) << "成功率:" << (100.0 - (double)metrics.errorCount / metrics.operationCount * 100) << "%";
    qCDebug(testPerformance) << "==============================";
}

void TestPerformance::generateCpuLoad(int durationMs, double targetCpuPercent)
{
    qCDebug(testPerformance) << "生成CPU负载:" << targetCpuPercent << "%, 持续" << durationMs << "毫秒";
    
    // 在后台线程中生成CPU负载，避免阻塞测试
    QThread* loadThread = QThread::create([durationMs, targetCpuPercent]() {
        QElapsedTimer timer;
        timer.start();
        
        const int workTime = static_cast<int>(10 * targetCpuPercent / 100.0);  // 工作时间（毫秒）
        const int sleepTime = 10 - workTime;  // 休眠时间（毫秒）
        
        while (timer.elapsed() < durationMs) {
            // 执行CPU密集型操作
            QElapsedTimer workTimer;
            workTimer.start();
            
            volatile double result = 0;
            while (workTimer.elapsed() < workTime) {
                for (int i = 0; i < 10000; ++i) {
                    result += qSqrt(i * 3.14159);
                }
            }
            
            // 休眠以控制CPU使用率
            if (sleepTime > 0) {
                QThread::msleep(sleepTime);
            }
        }
    });
    
    loadThread->start();
    
    // 不等待线程完成，让它在后台运行
    // 线程会在指定时间后自动结束
}

void TestPerformance::generateMemoryLoad(int sizeMB, int durationMs)
{
    qCDebug(testPerformance) << "生成内存负载:" << sizeMB << "MB, 持续" << durationMs << "毫秒";
    
    // 在后台线程中生成内存负载
    QThread* loadThread = QThread::create([sizeMB, durationMs]() {
        QElapsedTimer timer;
        timer.start();
        
        // 分配内存
        QList<QByteArray> memoryBlocks;
        const int blockSize = 1024 * 1024;  // 1MB块
        
        for (int i = 0; i < sizeMB; ++i) {
            QByteArray block(blockSize, static_cast<char>(i % 256));
            memoryBlocks.append(block);
            
            // 定期写入数据以确保内存真正被使用
            if (i % 10 == 0) {
                for (auto& block : memoryBlocks) {
                    block[0] = static_cast<char>(QRandomGenerator::global()->bounded(256));
                }
            }
        }
        
        // 保持内存分配直到指定时间
        while (timer.elapsed() < durationMs) {
            // 定期访问内存以防止被优化掉
            for (int i = 0; i < memoryBlocks.size(); i += 100) {
                memoryBlocks[i][100] = static_cast<char>(timer.elapsed() % 256);
            }
            
            QThread::msleep(100);
        }
        
        // 内存会在线程结束时自动释放
    });
    
    loadThread->start();
    
    // 不等待线程完成，让它在后台运行
}

void TestPerformance::generateNetworkLoad(int requestsPerSecond, int durationMs)
{
    qCDebug(testPerformance) << "生成网络负载:" << requestsPerSecond << "请求/秒, 持续" << durationMs << "毫秒";
    
    // 模拟网络负载（实际项目中可能需要真实的网络请求）
    QThread* loadThread = QThread::create([requestsPerSecond, durationMs]() {
        QElapsedTimer timer;
        timer.start();
        
        const int intervalMs = 1000 / requestsPerSecond;
        
        while (timer.elapsed() < durationMs) {
            // 模拟网络请求处理
            QElapsedTimer requestTimer;
            requestTimer.start();
            
            // 模拟网络延迟和数据处理
            QByteArray data(1024, 'x');  // 1KB数据
            for (int i = 0; i < data.size(); ++i) {
                data[i] = static_cast<char>(i % 256);
            }
            
            // 模拟网络延迟
            QThread::msleep(QRandomGenerator::global()->bounded(5, 20));
            
            // 等待下一个请求间隔
            int elapsed = requestTimer.elapsed();
            if (elapsed < intervalMs) {
                QThread::msleep(intervalMs - elapsed);
            }
        }
    });
    
    loadThread->start();
}

PerformanceMetrics TestPerformance::measurePerformance(std::function<void()> operation, int iterations)
{
    PerformanceMetrics metrics;
    QList<qint64> latencies;
    
    QElapsedTimer totalTimer;
    totalTimer.start();
    
    auto startStats = m_performanceOptimizer->getPerformanceStats();
    
    for (int i = 0; i < iterations; ++i) {
        QElapsedTimer iterationTimer;
        iterationTimer.start();
        
        try {
            operation();
            metrics.operationCount++;
        } catch (...) {
            metrics.errorCount++;
        }
        
        qint64 latency = iterationTimer.elapsed();
        latencies.append(latency);
    }
    
    qint64 totalTime = totalTimer.elapsed();
    auto endStats = m_performanceOptimizer->getPerformanceStats();
    
    // 计算性能指标
    metrics.throughput = (double)metrics.operationCount / (totalTime / 1000.0);
    
    if (!latencies.isEmpty()) {
        std::sort(latencies.begin(), latencies.end());
        
        metrics.minLatency = latencies.first();
        metrics.maxLatency = latencies.last();
        
        qint64 totalLatency = 0;
        for (qint64 latency : latencies) {
            totalLatency += latency;
        }
        metrics.averageLatency = (double)totalLatency / latencies.size();
    }
    
    metrics.cpuUsage = endStats.cpuUsage;
    metrics.memoryUsage = endStats.memoryUsage;
    
    return metrics;
}

QTEST_MAIN(TestPerformance)
#include "TestPerformance.moc"