/**
 * @file test_integration.cpp
 * @brief 集成测试 - 测试各个组件之间的协作
 * @author Assistant
 * @date 2024
 */

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QTimer>
#include <QThread>
#include <QtTest/QSignalSpy>
#include <QLoggingCategory>
#include <QDebug>
#include <memory>

// 包含被测试的类
#include "ThreadManager.h"
#include "ScreenCaptureWorker.h"
#include "PerformanceOptimizer.h"

// 日志分类
Q_LOGGING_CATEGORY(testIntegration, "test.integration")

/**
 * @class TestIntegration
 * @brief 集成测试类 - 测试各个组件之间的协作
 */
class TestIntegration : public QObject
{
    Q_OBJECT

public:
    TestIntegration();
    ~TestIntegration();

private slots:
    // 测试初始化和清理
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基本集成测试
    void test_threadManagerAndWorkerIntegration();
    void test_screenCaptureWorkerAndMessageQueueIntegration();
    void test_performanceOptimizerAndThreadManagerIntegration();
    
    // 复杂场景测试
    void test_multipleWorkersCoordination();
    void test_performanceOptimizationUnderLoad();
    void test_errorHandlingAcrossComponents();
    
    // 生命周期测试
    void test_componentStartupSequence();
    void test_componentShutdownSequence();
    void test_componentRestart();
    
    // 性能集成测试
    void test_memoryUsageUnderLoad();
    void test_threadPoolEfficiency();
    void test_messageQueueThroughput();
    
    // 稳定性测试
    void test_longRunningStability();
    void test_resourceLeakDetection();
    void test_concurrentOperations();

private:
    // 辅助方法
    void waitForCondition(std::function<bool()> condition, int timeoutMs = 5000);
    void simulateLoad(int durationMs = 1000);
    bool checkMemoryUsage();
    void verifyComponentStates();
    
    // 测试数据
    ThreadManager* m_threadManager;
    PerformanceOptimizer* m_performanceOptimizer;
    QList<ScreenCaptureWorker*> m_workers;
    // 已移除消息队列测试成员

    // 测试配置
    static const int MAX_WORKERS = 3;
    static const int TEST_TIMEOUT = 10000; // 10秒
    static const int LOAD_TEST_DURATION = 5000; // 5秒
};

TestIntegration::TestIntegration()
    : m_threadManager(nullptr)
    , m_performanceOptimizer(nullptr)
{
    qCDebug(testIntegration) << "TestIntegration构造函数";
}

TestIntegration::~TestIntegration()
{
    qCDebug(testIntegration) << "TestIntegration析构函数";
}

void TestIntegration::initTestCase()
{
    qCDebug(testIntegration) << "初始化测试套件";
    
    // 设置测试环境
    QCoreApplication::setApplicationName("QtRemoteDesktopIntegrationTest");
    QCoreApplication::setApplicationVersion("1.0.0");
    
    // 初始化日志
    QLoggingCategory::setFilterRules("test.integration.debug=true");
    
    qCDebug(testIntegration) << "测试套件初始化完成";
}

void TestIntegration::cleanupTestCase()
{
    qCDebug(testIntegration) << "清理测试套件";
    
    // 清理全局资源
    if (m_threadManager) {
        m_threadManager->destroyAllThreads();
    }
    
    qCDebug(testIntegration) << "测试套件清理完成";
}

void TestIntegration::init()
{
    qCDebug(testIntegration) << "初始化测试用例";
    
    // 获取单例实例（注意：instance() 返回指针，无需取地址）
    m_threadManager = ThreadManager::instance();
    m_performanceOptimizer = PerformanceOptimizer::instance();
    
    // 清理之前的状态
    m_threadManager->destroyAllThreads();
    m_workers.clear();
    
    // 等待清理完成
    QTest::qWait(200);
    
    qCDebug(testIntegration) << "测试用例初始化完成";
}

void TestIntegration::cleanup()
{
    qCDebug(testIntegration) << "清理测试用例";
    
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
    
    // 等待清理完成
    QTest::qWait(200);
    
    qCDebug(testIntegration) << "测试用例清理完成";
}

void TestIntegration::test_threadManagerAndWorkerIntegration()
{
    qCDebug(testIntegration) << "测试ThreadManager和Worker集成";
    
    // 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    // 通过ThreadManager创建线程
    QString threadId = m_threadManager->createThread("TestWorker", worker);
    QVERIFY(!threadId.isEmpty());
    
    // 启动线程
    bool started = m_threadManager->startThread(threadId);
    QVERIFY(started);
    
    // 等待线程启动
    waitForCondition([this, threadId]() {
        return m_threadManager->isThreadRunning(threadId);
    });
    
    QVERIFY(m_threadManager->isThreadRunning(threadId));
    
    // 获取线程信息
    auto threadInfo = m_threadManager->getThreadInfo(threadId);
    QVERIFY(threadInfo != nullptr);
    QCOMPARE(threadInfo->name, QString("TestWorker"));
    // 注意：ThreadInfo结构中没有status字段，这里只验证线程信息存在
    QVERIFY(!threadInfo->name.isEmpty());
    
    // 停止线程
    bool stopped = m_threadManager->stopThread(threadId);
    QVERIFY(stopped);
    
    // 等待线程停止
    waitForCondition([this, threadId]() {
        return !m_threadManager->isThreadRunning(threadId);
    });
    
    QVERIFY(!m_threadManager->isThreadRunning(threadId));
    
    qCDebug(testIntegration) << "ThreadManager和Worker集成测试完成";
}

void TestIntegration::test_screenCaptureWorkerAndMessageQueueIntegration()
{
    qCDebug(testIntegration) << "测试ScreenCaptureWorker和MessageQueue集成";
    
    // 移除旧消息队列依赖：直接基于信号流验证
    
    // 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    // 连接信号槽
    QSignalSpy frameReadySpy(worker, &ScreenCaptureWorker::frameReady);
    QSignalSpy errorSpy(worker, &ScreenCaptureWorker::errorOccurred);
    
    // 通过ThreadManager创建线程
    QString threadId = m_threadManager->createThread("CaptureWorker", worker);
    QVERIFY(!threadId.isEmpty());
    
    // 启动线程
    bool started = m_threadManager->startThread(threadId);
    QVERIFY(started);
    
    // 等待线程启动
    waitForCondition([this, threadId]() {
        return m_threadManager->isThreadRunning(threadId);
    });
    
    // 配置捕获参数
    ScreenCaptureConfig config;
    config.frameRate = 10;
    config.quality = 80;
    config.captureRegion = QRect(0, 0, 800, 600);
    
    worker->configure(config);
    
    // 开始捕获
    worker->startCapture();
    
    // 等待一些帧数据
    bool framesReceived = frameReadySpy.wait(3000);
    QVERIFY(framesReceived);
    QVERIFY(frameReadySpy.count() > 0);
    
    // 验证没有错误
    QCOMPARE(errorSpy.count(), 0);
    
    // 停止捕获
    worker->stopCapture();
    
    // 停止线程
    m_threadManager->stopThread(threadId);
    
    qCDebug(testIntegration) << "ScreenCaptureWorker和MessageQueue集成测试完成";
}

void TestIntegration::test_performanceOptimizerAndThreadManagerIntegration()
{
    qCDebug(testIntegration) << "测试PerformanceOptimizer和ThreadManager集成";
    
    // 启动性能监控
    m_performanceOptimizer->startMonitoring();
    
    // 创建多个工作线程
    QStringList threadIds;
    for (int i = 0; i < MAX_WORKERS; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        QString threadId = m_threadManager->createThread(
            QString("Worker_%1").arg(i), worker);
        QVERIFY(!threadId.isEmpty());
        threadIds.append(threadId);
        
        bool started = m_threadManager->startThread(threadId);
        QVERIFY(started);
    }
    
    // 等待所有线程启动
    for (const QString& threadId : threadIds) {
        waitForCondition([this, threadId]() {
            return m_threadManager->isThreadRunning(threadId);
        });
    }
    
    // 模拟负载
    simulateLoad(2000);
    
    // 检查性能统计
    auto stats = m_performanceOptimizer->getPerformanceStats();
    QVERIFY(stats.cpuUsage >= 0.0 && stats.cpuUsage <= 100.0);
    QVERIFY(stats.memoryUsage >= 0.0);
    QVERIFY(stats.threadCount >= MAX_WORKERS);
    
    // 触发优化
    m_performanceOptimizer->optimizePerformance();
    
    // 等待优化完成
    QTest::qWait(1000);
    
    // 停止所有线程
    for (const QString& threadId : threadIds) {
        m_threadManager->stopThread(threadId);
    }
    
    // 停止性能监控
    m_performanceOptimizer->stopMonitoring();
    
    qCDebug(testIntegration) << "PerformanceOptimizer和ThreadManager集成测试完成";
}

void TestIntegration::test_multipleWorkersCoordination()
{
    qCDebug(testIntegration) << "测试多个Worker协调";
    
    // 创建多个工作线程
    QStringList threadIds;
    QList<QSignalSpy*> spies;
    
    for (int i = 0; i < MAX_WORKERS; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        // 创建信号监听器
        auto* spy = new QSignalSpy(worker, &ScreenCaptureWorker::frameReady);
        spies.append(spy);
        
        QString threadId = m_threadManager->createThread(
            QString("CoordWorker_%1").arg(i), worker);
        threadIds.append(threadId);
        
        m_threadManager->startThread(threadId);
    }
    
    // 等待所有线程启动
    for (const QString& threadId : threadIds) {
        waitForCondition([this, threadId]() {
            return m_threadManager->isThreadRunning(threadId);
        });
    }
    
    // 配置所有工作线程
    ScreenCaptureConfig config;
    config.frameRate = 5;
    config.quality = 60;
    
    for (auto* worker : m_workers) {
        worker->configure(config);
        worker->startCapture();
    }
    
    // 等待所有工作线程产生数据
    QTest::qWait(3000);
    
    // 验证所有工作线程都在工作
    for (auto* spy : spies) {
        QVERIFY(spy->count() > 0);
    }
    
    // 停止所有工作线程
    for (auto* worker : m_workers) {
        worker->stopCapture();
    }
    
    for (const QString& threadId : threadIds) {
        m_threadManager->stopThread(threadId);
    }
    
    // 清理信号监听器
    qDeleteAll(spies);
    
    qCDebug(testIntegration) << "多个Worker协调测试完成";
}

void TestIntegration::test_performanceOptimizationUnderLoad()
{
    qCDebug(testIntegration) << "测试负载下的性能优化";
    
    // 启动性能监控
    m_performanceOptimizer->startMonitoring();
    
    // 创建高负载场景
    QStringList threadIds;
    for (int i = 0; i < MAX_WORKERS * 2; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        QString threadId = m_threadManager->createThread(
            QString("LoadWorker_%1").arg(i), worker);
        threadIds.append(threadId);
        
        m_threadManager->startThread(threadId);
        
        // 配置高帧率
        ScreenCaptureConfig config;
        config.frameRate = 30;
        config.quality = 90;
        worker->configure(config);
        worker->startCapture();
    }
    
    // 运行负载测试
    simulateLoad(LOAD_TEST_DURATION);
    
    // 检查性能指标
    auto stats = m_performanceOptimizer->getPerformanceStats();
    qCDebug(testIntegration) << "负载测试性能统计:" 
                            << "CPU:" << stats.cpuUsage << "%"
                            << "内存:" << stats.memoryUsage << "MB"
                            << "线程数:" << stats.threadCount;
    
    // 触发自动优化
    m_performanceOptimizer->enableAutoOptimization(true);
    
    // 等待优化生效
    QTest::qWait(2000);
    
    // 再次检查性能
    auto optimizedStats = m_performanceOptimizer->getPerformanceStats();
    qCDebug(testIntegration) << "优化后性能统计:"
                            << "CPU:" << optimizedStats.cpuUsage << "%"
                            << "内存:" << optimizedStats.memoryUsage << "MB";
    
    // 停止所有工作线程
    for (auto* worker : m_workers) {
        worker->stopCapture();
    }
    
    for (const QString& threadId : threadIds) {
        m_threadManager->stopThread(threadId);
    }
    
    m_performanceOptimizer->stopMonitoring();
    
    qCDebug(testIntegration) << "负载下性能优化测试完成";
}

void TestIntegration::test_errorHandlingAcrossComponents()
{
    qCDebug(testIntegration) << "测试跨组件错误处理";
    
    // 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QSignalSpy errorSpy(worker, &ScreenCaptureWorker::errorOccurred);
    
    QString threadId = m_threadManager->createThread("ErrorTestWorker", worker);
    m_threadManager->startThread(threadId);
    
    // 等待线程启动
    waitForCondition([this, threadId]() {
        return m_threadManager->isThreadRunning(threadId);
    });
    
    // 配置无效参数触发错误
    ScreenCaptureConfig invalidConfig;
    invalidConfig.frameRate = -1;  // 无效帧率
    invalidConfig.quality = 150;   // 无效质量
    
    worker->configure(invalidConfig);
    worker->startCapture();
    
    // 等待错误信号
    bool errorOccurred = errorSpy.wait(3000);
    
    // 验证错误处理
    if (errorOccurred) {
        QVERIFY(errorSpy.count() > 0);
        qCDebug(testIntegration) << "成功捕获到错误信号";
    }
    
    // 验证线程管理器能够处理错误状态
    auto threadInfo = m_threadManager->getThreadInfo(threadId);
    QVERIFY(threadInfo != nullptr);
    
    // 停止线程
    m_threadManager->stopThread(threadId);
    
    qCDebug(testIntegration) << "跨组件错误处理测试完成";
}

void TestIntegration::test_componentStartupSequence()
{
    qCDebug(testIntegration) << "测试组件启动序列";
    
    // 1. 首先启动性能优化器
    m_performanceOptimizer->startMonitoring();
    QVERIFY(m_performanceOptimizer->isMonitoring());
    
    // 2. 创建线程管理器实例
    QVERIFY(m_threadManager != nullptr);
    
    // 3. 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QString threadId = m_threadManager->createThread("StartupWorker", worker);
    QVERIFY(!threadId.isEmpty());
    
    // 4. 启动工作线程
    bool started = m_threadManager->startThread(threadId);
    QVERIFY(started);
    
    // 5. 验证启动序列
    waitForCondition([this, threadId]() {
        return m_threadManager->isThreadRunning(threadId);
    });
    
    QVERIFY(m_threadManager->isThreadRunning(threadId));
    
    // 6. 验证性能监控正在工作
    auto stats = m_performanceOptimizer->getPerformanceStats();
    QVERIFY(stats.threadCount > 0);
    
    // 清理
    m_threadManager->stopThread(threadId);
    m_performanceOptimizer->stopMonitoring();
    
    qCDebug(testIntegration) << "组件启动序列测试完成";
}

void TestIntegration::test_componentShutdownSequence()
{
    qCDebug(testIntegration) << "测试组件关闭序列";
    
    // 设置测试环境
    m_performanceOptimizer->startMonitoring();
    
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QString threadId = m_threadManager->createThread("ShutdownWorker", worker);
    m_threadManager->startThread(threadId);
    
    waitForCondition([this, threadId]() {
        return m_threadManager->isThreadRunning(threadId);
    });
    
    // 开始关闭序列
    // 1. 停止工作线程
    worker->stopCapture();
    
    // 2. 停止线程
    bool stopped = m_threadManager->stopThread(threadId);
    QVERIFY(stopped);
    
    waitForCondition([this, threadId]() {
        return !m_threadManager->isThreadRunning(threadId);
    });
    
    // 3. 销毁线程
    bool destroyed = m_threadManager->destroyThread(threadId);
    QVERIFY(destroyed);
    
    // 4. 停止性能监控
    m_performanceOptimizer->stopMonitoring();
    QVERIFY(!m_performanceOptimizer->isMonitoring());
    
    qCDebug(testIntegration) << "组件关闭序列测试完成";
}

void TestIntegration::test_componentRestart()
{
    qCDebug(testIntegration) << "测试组件重启";
    
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QString threadId = m_threadManager->createThread("RestartWorker", worker);
    
    // 第一次启动
    m_threadManager->startThread(threadId);
    waitForCondition([this, threadId]() {
        return m_threadManager->isThreadRunning(threadId);
    });
    QVERIFY(m_threadManager->isThreadRunning(threadId));
    
    // 停止
    m_threadManager->stopThread(threadId);
    waitForCondition([this, threadId]() {
        return !m_threadManager->isThreadRunning(threadId);
    });
    QVERIFY(!m_threadManager->isThreadRunning(threadId));
    
    // 重新启动
    m_threadManager->startThread(threadId);
    waitForCondition([this, threadId]() {
        return m_threadManager->isThreadRunning(threadId);
    });
    QVERIFY(m_threadManager->isThreadRunning(threadId));
    
    // 最终清理
    m_threadManager->stopThread(threadId);
    
    qCDebug(testIntegration) << "组件重启测试完成";
}

void TestIntegration::test_memoryUsageUnderLoad()
{
    qCDebug(testIntegration) << "测试负载下的内存使用";
    
    m_performanceOptimizer->startMonitoring();
    
    // 记录初始内存使用
    auto initialStats = m_performanceOptimizer->getPerformanceStats();
    double initialMemory = initialStats.memoryUsage;
    
    // 创建多个工作线程
    QStringList threadIds;
    for (int i = 0; i < MAX_WORKERS; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        QString threadId = m_threadManager->createThread(
            QString("MemoryWorker_%1").arg(i), worker);
        threadIds.append(threadId);
        
        m_threadManager->startThread(threadId);
        worker->startCapture();
    }
    
    // 运行一段时间
    simulateLoad(3000);
    
    // 检查内存使用
    auto loadStats = m_performanceOptimizer->getPerformanceStats();
    double loadMemory = loadStats.memoryUsage;
    
    qCDebug(testIntegration) << "内存使用情况:"
                            << "初始:" << initialMemory << "MB"
                            << "负载:" << loadMemory << "MB"
                            << "增长:" << (loadMemory - initialMemory) << "MB";
    
    // 验证内存使用在合理范围内
    QVERIFY(loadMemory > initialMemory);  // 应该有所增长
    QVERIFY((loadMemory - initialMemory) < 500);  // 但不应该增长太多
    
    // 停止所有工作线程
    for (auto* worker : m_workers) {
        worker->stopCapture();
    }
    
    for (const QString& threadId : threadIds) {
        m_threadManager->stopThread(threadId);
    }
    
    // 等待内存释放
    QTest::qWait(2000);
    
    // 检查内存是否释放
    auto finalStats = m_performanceOptimizer->getPerformanceStats();
    double finalMemory = finalStats.memoryUsage;
    
    qCDebug(testIntegration) << "最终内存:" << finalMemory << "MB";
    
    m_performanceOptimizer->stopMonitoring();
    
    qCDebug(testIntegration) << "负载下内存使用测试完成";
}

void TestIntegration::test_threadPoolEfficiency()
{
    qCDebug(testIntegration) << "测试线程池效率";
    
    // 测试线程创建效率
    QElapsedTimer timer;
    timer.start();
    
    QStringList threadIds;
    for (int i = 0; i < MAX_WORKERS * 2; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        QString threadId = m_threadManager->createThread(
            QString("EfficiencyWorker_%1").arg(i), worker);
        threadIds.append(threadId);
    }
    
    qint64 creationTime = timer.elapsed();
    qCDebug(testIntegration) << "创建" << threadIds.size() << "个线程耗时:" << creationTime << "ms";
    
    // 测试线程启动效率
    timer.restart();
    
    for (const QString& threadId : threadIds) {
        m_threadManager->startThread(threadId);
    }
    
    // 等待所有线程启动
    for (const QString& threadId : threadIds) {
        waitForCondition([this, threadId]() {
            return m_threadManager->isThreadRunning(threadId);
        }, 1000);
    }
    
    qint64 startupTime = timer.elapsed();
    qCDebug(testIntegration) << "启动" << threadIds.size() << "个线程耗时:" << startupTime << "ms";
    
    // 验证效率
    QVERIFY(creationTime < 1000);  // 创建应该在1秒内完成
    QVERIFY(startupTime < 2000);   // 启动应该在2秒内完成
    
    // 测试线程停止效率
    timer.restart();
    
    for (const QString& threadId : threadIds) {
        m_threadManager->stopThread(threadId);
    }
    
    qint64 stopTime = timer.elapsed();
    qCDebug(testIntegration) << "停止" << threadIds.size() << "个线程耗时:" << stopTime << "ms";
    
    QVERIFY(stopTime < 2000);  // 停止应该在2秒内完成
    
    qCDebug(testIntegration) << "线程池效率测试完成";
}

void TestIntegration::test_messageQueueThroughput()
{
    qCDebug(testIntegration) << "测试消息队列吞吐量（已切换为信号吞吐量验证）";
    
    // 直接通过高帧率配置与frameReady信号统计吞吐量
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    QSignalSpy frameReadySpy(worker, &ScreenCaptureWorker::frameReady);
    
    QString threadId = m_threadManager->createThread("ThroughputWorker", worker);
    m_threadManager->startThread(threadId);
    
    waitForCondition([this, threadId]() {
        return m_threadManager->isThreadRunning(threadId);
    });
    
    ScreenCaptureConfig config;
    config.frameRate = 60;  // 高帧率
    config.quality = 50;    // 中等质量以提高速度
    worker->configure(config);
    worker->startCapture();
    
    QElapsedTimer timer;
    timer.start();
    QTest::qWait(5000); // 运行5秒
    
    qint64 elapsed = timer.elapsed();
    int frameCount = frameReadySpy.count();
    double throughput = (double)frameCount / (elapsed / 1000.0);
    
    qCDebug(testIntegration) << "信号吞吐量: 帧数:" << frameCount
                             << "时间:" << elapsed << "ms"
                             << "吞吐量:" << throughput << "帧/秒";
    
    QVERIFY(frameCount > 0);
    QVERIFY(throughput > 0);
    
    worker->stopCapture();
    m_threadManager->stopThread(threadId);
    
    qCDebug(testIntegration) << "消息队列吞吐量测试完成（信号方式）";
}

void TestIntegration::test_longRunningStability()
{
    qCDebug(testIntegration) << "测试长时间运行稳定性";
    
    m_performanceOptimizer->startMonitoring();
    
    // 创建工作线程
    auto* worker = new ScreenCaptureWorker();
    m_workers.append(worker);
    
    QSignalSpy errorSpy(worker, &ScreenCaptureWorker::errorOccurred);
    
    QString threadId = m_threadManager->createThread("StabilityWorker", worker);
    m_threadManager->startThread(threadId);
    
    waitForCondition([this, threadId]() {
        return m_threadManager->isThreadRunning(threadId);
    });
    
    // 配置适中的参数
    ScreenCaptureConfig config;
    config.frameRate = 15;
    config.quality = 70;
    
    worker->configure(config);
    worker->startCapture();
    
    // 运行较长时间（10秒）
    const int testDuration = 10000;
    const int checkInterval = 2000;
    
    for (int i = 0; i < testDuration; i += checkInterval) {
        QTest::qWait(checkInterval);
        
        // 检查线程是否还在运行
        QVERIFY(m_threadManager->isThreadRunning(threadId));
        
        // 检查是否有错误
        QCOMPARE(errorSpy.count(), 0);
        
        // 检查内存使用
        QVERIFY(checkMemoryUsage());
        
        qCDebug(testIntegration) << "稳定性检查" << (i + checkInterval) / 1000 << "秒";
    }
    
    worker->stopCapture();
    m_threadManager->stopThread(threadId);
    m_performanceOptimizer->stopMonitoring();
    
    qCDebug(testIntegration) << "长时间运行稳定性测试完成";
}

void TestIntegration::test_resourceLeakDetection()
{
    qCDebug(testIntegration) << "测试资源泄漏检测";
    
    m_performanceOptimizer->startMonitoring();
    
    // 记录初始资源使用
    auto initialStats = m_performanceOptimizer->getPerformanceStats();
    
    // 多次创建和销毁线程
    for (int cycle = 0; cycle < 5; ++cycle) {
        qCDebug(testIntegration) << "资源泄漏检测循环" << cycle + 1;
        
        QStringList threadIds;
        
        // 创建线程
        for (int i = 0; i < MAX_WORKERS; ++i) {
            auto* worker = new ScreenCaptureWorker();
            m_workers.append(worker);
            
            QString threadId = m_threadManager->createThread(
                QString("LeakTestWorker_%1_%2").arg(cycle).arg(i), worker);
            threadIds.append(threadId);
            
            m_threadManager->startThread(threadId);
            worker->startCapture();
        }
        
        // 运行一段时间
        QTest::qWait(1000);
        
        // 停止和销毁线程
        for (auto* worker : m_workers) {
            worker->stopCapture();
        }
        
        for (const QString& threadId : threadIds) {
            m_threadManager->stopThread(threadId);
            m_threadManager->destroyThread(threadId);
        }
        
        // 清理工作线程列表
        m_workers.clear();
        
        // 等待资源释放
        QTest::qWait(500);
        
        // 检查资源使用
        auto currentStats = m_performanceOptimizer->getPerformanceStats();
        qCDebug(testIntegration) << "循环" << cycle + 1 << "内存使用:" << currentStats.memoryUsage << "MB";
    }
    
    // 最终检查
    auto finalStats = m_performanceOptimizer->getPerformanceStats();
    
    qCDebug(testIntegration) << "资源使用对比:"
                            << "初始:" << initialStats.memoryUsage << "MB"
                            << "最终:" << finalStats.memoryUsage << "MB"
                            << "差异:" << (finalStats.memoryUsage - initialStats.memoryUsage) << "MB";
    
    // 验证没有严重的内存泄漏
    double memoryIncrease = finalStats.memoryUsage - initialStats.memoryUsage;
    QVERIFY(memoryIncrease < 100);  // 内存增长应该小于100MB
    
    m_performanceOptimizer->stopMonitoring();
    
    qCDebug(testIntegration) << "资源泄漏检测测试完成";
}

void TestIntegration::test_concurrentOperations()
{
    qCDebug(testIntegration) << "测试并发操作";
    
    // 创建多个工作线程
    QStringList threadIds;
    QList<QSignalSpy*> spies;
    
    for (int i = 0; i < MAX_WORKERS; ++i) {
        auto* worker = new ScreenCaptureWorker();
        m_workers.append(worker);
        
        auto* spy = new QSignalSpy(worker, &ScreenCaptureWorker::frameReady);
        spies.append(spy);
        
        QString threadId = m_threadManager->createThread(
            QString("ConcurrentWorker_%1").arg(i), worker);
        threadIds.append(threadId);
    }
    
    // 并发启动所有线程
    QElapsedTimer timer;
    timer.start();
    
    for (const QString& threadId : threadIds) {
        m_threadManager->startThread(threadId);
    }
    
    // 等待所有线程启动
    for (const QString& threadId : threadIds) {
        waitForCondition([this, threadId]() {
            return m_threadManager->isThreadRunning(threadId);
        });
    }
    
    qint64 startupTime = timer.elapsed();
    qCDebug(testIntegration) << "并发启动" << threadIds.size() << "个线程耗时:" << startupTime << "ms";
    
    // 并发配置和启动捕获
    for (int i = 0; i < m_workers.size(); ++i) {
        ScreenCaptureConfig config;
        config.frameRate = 10 + (i * 5);  // 不同的帧率
        config.quality = 60 + (i * 10);   // 不同的质量
        
        m_workers[i]->configure(config);
        m_workers[i]->startCapture();
    }
    
    // 运行并发操作
    QTest::qWait(3000);
    
    // 验证所有线程都在正常工作
    for (int i = 0; i < spies.size(); ++i) {
        QVERIFY(spies[i]->count() > 0);
        qCDebug(testIntegration) << "工作线程" << i << "产生了" << spies[i]->count() << "帧";
    }
    
    // 并发停止
    timer.restart();
    
    for (auto* worker : m_workers) {
        worker->stopCapture();
    }
    
    for (const QString& threadId : threadIds) {
        m_threadManager->stopThread(threadId);
    }
    
    qint64 stopTime = timer.elapsed();
    qCDebug(testIntegration) << "并发停止" << threadIds.size() << "个线程耗时:" << stopTime << "ms";
    
    // 清理
    qDeleteAll(spies);
    
    qCDebug(testIntegration) << "并发操作测试完成";
}

// 辅助方法实现
void TestIntegration::waitForCondition(std::function<bool()> condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
}

void TestIntegration::simulateLoad(int durationMs)
{
    QElapsedTimer timer;
    timer.start();
    
    while (timer.elapsed() < durationMs) {
        // 模拟CPU负载
        volatile int dummy = 0;
        for (int i = 0; i < 10000; ++i) {
            dummy += i;
        }
        
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
}

bool TestIntegration::checkMemoryUsage()
{
    if (!m_performanceOptimizer->isMonitoring()) {
        return true;  // 如果没有监控，假设正常
    }
    
    auto stats = m_performanceOptimizer->getPerformanceStats();
    
    // 检查内存使用是否在合理范围内（小于1GB）
    return stats.memoryUsage < 1024.0;
}

void TestIntegration::verifyComponentStates()
{
    // 验证ThreadManager状态
    QVERIFY(m_threadManager != nullptr);
    
    // 验证PerformanceOptimizer状态
    QVERIFY(m_performanceOptimizer != nullptr);
    
    // 验证所有工作线程状态
    for (auto* worker : m_workers) {
        QVERIFY(worker != nullptr);
    }
}

// 包含MOC文件
#include "TestIntegration.moc"

// 主函数
QTEST_MAIN(TestIntegration)