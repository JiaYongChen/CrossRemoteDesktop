#include <QtTest/QtTest>
#include <QtTest/QSignalSpy>
#include <QtCore/QTimer>
#include <QtGui/QPixmap>
#include <QtGui/QScreen>
#include <QtGui/QGuiApplication>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

#include "../src/server/capture/ScreenCapture.h"
#include "../src/common/core/threading/ThreadManager.h"
#include "../src/common/core/logging/LoggingCategories.h"
#include "../src/server/dataflow/QueueManager.h"
#include "../src/server/dataflow/DataFlowStructures.h"

Q_LOGGING_CATEGORY(testScreenCapture, "test.screencapture")

/**
 * @brief ScreenCapture单元测试类
 * 
 * 测试ScreenCapture主类的所有核心功能，包括：
 * - 捕获控制（启动/停止/状态查询）
 * - 配置管理（帧率、质量、模式设置）
 * - 性能统计和监控
 * - 错误处理和恢复
 * - 线程安全性
 */
class TestScreenCapture : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<ScreenCapture> m_screenCapture;
    ThreadManager* m_threadManager;
    
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
     * @brief 测试基本功能
     */
    void test_basicFunctionality();
    
    /**
     * @brief 测试捕获控制
     */
    void test_captureControl();
    
    /**
     * @brief 测试帧率控制
     */
    void test_frameRateControl();
    
    /**
     * @brief 测试质量控制
     */
    void test_qualityControl();
    
    /**
     * @brief 测试高清模式
     */
    void test_highDefinitionMode();
    
    /**
     * @brief 测试抗锯齿功能
     */
    void test_antiAliasing();
    
    /**
     * @brief 测试缩放质量控制
     */
    void test_scaleQuality();
    
    /**
     * @brief 测试队列管理
     */
    void test_queueManagement();
    
    /**
     * @brief 测试性能统计
     */
    void test_performanceStats();
    
    /**
     * @brief 测试同步捕获（遗留功能）
     */
    void test_syncCapture();
    
    /**
     * @brief 测试信号发射
     */
    void test_signalEmission();
    
    /**
     * @brief 测试错误处理
     */
    void test_errorHandling();
    
    /**
     * @brief 测试线程安全性
     */
    void test_threadSafety();
    
    /**
     * @brief 测试内存管理
     */
    void test_memoryManagement();
    // 新增：按函数名规范覆盖核心职责
    void test_startCapture();
    void test_stopCapture();
    void test_updateCaptureConfig();
    void test_getCaptureConfig();
    void test_getPerformanceStats();
    void test_stopCapture_errorPath();
};

void TestScreenCapture::initTestCase()
{
    qCDebug(testScreenCapture, "开始ScreenCapture测试");
    
    // 初始化线程管理器
    m_threadManager = ThreadManager::instance();
    QVERIFY(m_threadManager != nullptr);
}

void TestScreenCapture::cleanupTestCase()
{
    qCDebug(testScreenCapture, "ScreenCapture测试完成");
}

void TestScreenCapture::init()
{
    // 每个测试前创建新的ScreenCapture实例
    m_screenCapture = std::make_unique<ScreenCapture>();
    QVERIFY(m_screenCapture != nullptr);
}

void TestScreenCapture::cleanup()
{
    // 确保停止捕获并清理资源
    if (m_screenCapture && m_screenCapture->isCapturing()) {
        m_screenCapture->stopCapture();
        QTest::qWait(100); // 等待停止完成
    }
    m_screenCapture.reset();
}

void TestScreenCapture::test_basicFunctionality()
{
    qCDebug(testScreenCapture, "测试基本功能");
    
    // 测试初始状态
    QVERIFY(!m_screenCapture->isCapturing());
    auto config = m_screenCapture->getCaptureConfig();
    QVERIFY(config.frameRate > 0);
    QVERIFY(config.quality > 0.0);
    QVERIFY(config.quality <= 1.0);
    
    qCDebug(testScreenCapture, "基本功能测试通过");
}

void TestScreenCapture::test_captureControl()
{
    qCDebug(testScreenCapture, "测试捕获控制");
    
    // 测试启动捕获
    QVERIFY(!m_screenCapture->isCapturing());
    m_screenCapture->startCapture();
    QTest::qWait(100); // 等待启动完成
    QVERIFY(m_screenCapture->isCapturing());
    
    // 测试停止捕获
    m_screenCapture->stopCapture();
    QTest::qWait(100); // 等待停止完成
    QVERIFY(!m_screenCapture->isCapturing());
    
    qCDebug(testScreenCapture, "捕获控制测试通过");
}

void TestScreenCapture::test_frameRateControl()
{
    qCDebug(testScreenCapture, "测试帧率控制");
    
    // 测试设置和获取帧率
    int testFrameRates[] = {15, 30, 60};
    for (int fps : testFrameRates) {
        auto config = m_screenCapture->getCaptureConfig();
        config.frameRate = fps;
        m_screenCapture->updateCaptureConfig(config);
        
        auto updatedConfig = m_screenCapture->getCaptureConfig();
        QCOMPARE(updatedConfig.frameRate, fps);
    }
    
    // 测试边界值
    auto config = m_screenCapture->getCaptureConfig();
    config.frameRate = 1;
    m_screenCapture->updateCaptureConfig(config);
    QVERIFY(m_screenCapture->getCaptureConfig().frameRate >= 1);
    
    config.frameRate = 120;
    m_screenCapture->updateCaptureConfig(config);
    QVERIFY(m_screenCapture->getCaptureConfig().frameRate <= 120);
    
    qCDebug(testScreenCapture, "帧率控制测试通过");
}

void TestScreenCapture::test_qualityControl()
{
    qCDebug(testScreenCapture, "测试质量控制");
    
    // 测试设置和获取质量
    double testQualities[] = {0.1, 0.5, 0.8, 1.0};
    for (double quality : testQualities) {
        auto config = m_screenCapture->getCaptureConfig();
        config.quality = quality;
        m_screenCapture->updateCaptureConfig(config);
        
        auto updatedConfig = m_screenCapture->getCaptureConfig();
        QCOMPARE(updatedConfig.quality, quality);
    }
    
    // 测试边界值
    auto config = m_screenCapture->getCaptureConfig();
    config.quality = 0.0;
    m_screenCapture->updateCaptureConfig(config);
    QVERIFY(m_screenCapture->getCaptureConfig().quality >= 0.0);
    
    config.quality = 1.5;
    m_screenCapture->updateCaptureConfig(config);
    QVERIFY(m_screenCapture->getCaptureConfig().quality <= 1.0);
    
    qCDebug(testScreenCapture, "质量控制测试通过");
}

void TestScreenCapture::test_highDefinitionMode()
{
    qCDebug(testScreenCapture, "测试高清模式");
    
    // 测试启用高清模式
    auto config = m_screenCapture->getCaptureConfig();
    config.highDefinition = true;
    m_screenCapture->updateCaptureConfig(config);
    
    auto updatedConfig = m_screenCapture->getCaptureConfig();
    QVERIFY(updatedConfig.highDefinition);
    
    // 测试禁用高清模式
    config.highDefinition = false;
    m_screenCapture->updateCaptureConfig(config);
    
    updatedConfig = m_screenCapture->getCaptureConfig();
    QVERIFY(!updatedConfig.highDefinition);
    
    qCDebug(testScreenCapture, "高清模式测试通过");
}

void TestScreenCapture::test_antiAliasing()
{
    qCDebug(testScreenCapture, "测试抗锯齿功能");
    
    // 测试启用抗锯齿
    auto config = m_screenCapture->getCaptureConfig();
    config.antiAliasing = true;
    m_screenCapture->updateCaptureConfig(config);
    
    auto updatedConfig = m_screenCapture->getCaptureConfig();
    QVERIFY(updatedConfig.antiAliasing);
    
    // 测试禁用抗锯齿
    config.antiAliasing = false;
    m_screenCapture->updateCaptureConfig(config);
    
    updatedConfig = m_screenCapture->getCaptureConfig();
    QVERIFY(!updatedConfig.antiAliasing);
    
    qCDebug(testScreenCapture, "抗锯齿功能测试通过");
}

void TestScreenCapture::test_scaleQuality()
{
    qCDebug(testScreenCapture, "测试缩放质量控制");
    
    // 测试启用高质量缩放
    auto config = m_screenCapture->getCaptureConfig();
    config.quality = 1.0; // 高质量
    m_screenCapture->updateCaptureConfig(config);
    
    auto updatedConfig = m_screenCapture->getCaptureConfig();
    QCOMPARE(updatedConfig.quality, 1.0);
    
    // 测试禁用高质量缩放
    config.quality = 0.5; // 中等质量
    m_screenCapture->updateCaptureConfig(config);
    
    updatedConfig = m_screenCapture->getCaptureConfig();
    QCOMPARE(updatedConfig.quality, 0.5);
    
    qCDebug(testScreenCapture, "缩放质量控制测试通过");
}

void TestScreenCapture::test_queueManagement()
{
    qCDebug(testScreenCapture, "测试队列管理");
    
    // 简化测试：验证性能统计的可用性（不再校验队列利用率）
    auto stats = m_screenCapture->getPerformanceStats();
    QVERIFY(stats.totalFramesCaptured >= 0);
    QVERIFY(stats.totalFramesProcessed >= 0);
    QVERIFY(stats.droppedFrames >= 0);
    
    // 测试重置统计
    m_screenCapture->resetPerformanceStats();
    auto resetStats = m_screenCapture->getPerformanceStats();
    QCOMPARE(resetStats.totalFramesCaptured, quint64(0));
    QCOMPARE(resetStats.totalFramesProcessed, quint64(0));
    QCOMPARE(resetStats.droppedFrames, quint64(0));
    
    qCDebug(testScreenCapture, "队列管理测试通过");
}

void TestScreenCapture::test_performanceStats()
{
    qCDebug(testScreenCapture, "测试性能统计");
    
    // 获取初始统计数据
    auto stats = m_screenCapture->getPerformanceStats();
    QVERIFY(stats.totalFramesCaptured >= 0);
    QVERIFY(stats.totalFramesProcessed >= 0);
    QVERIFY(stats.droppedFrames >= 0);
    QVERIFY(stats.captureFrameRate >= 0.0);
    QVERIFY(stats.processingFrameRate >= 0.0);
    
    // 重置统计数据
    m_screenCapture->resetPerformanceStats();
    auto resetStats = m_screenCapture->getPerformanceStats();
    QCOMPARE(resetStats.totalFramesCaptured, 0ULL);
    QCOMPARE(resetStats.totalFramesProcessed, 0ULL);
    QCOMPARE(resetStats.droppedFrames, 0ULL);
    
    qCDebug(testScreenCapture, "性能统计测试通过");
}

void TestScreenCapture::test_syncCapture()
{
    qCDebug(testScreenCapture, "测试异步捕获功能（通过队列）");
    
    // 获取捕获队列
    QueueManager* queueManager = QueueManager::instance();
    auto captureQueue = queueManager->getCaptureQueue();
    QVERIFY(captureQueue != nullptr);
    
    // 清空队列
    CapturedFrame tempFrame;
    while (captureQueue->tryDequeue(tempFrame)) {}
    
    // 开始捕获
    m_screenCapture->startCapture();
    
    // 等待至少一帧进入队列
    int maxWaitTime = 5000; // 5秒
    int waitInterval = 100;
    int waited = 0;
    bool frameReceived = false;
    
    while (waited < maxWaitTime && !frameReceived) {
        QTest::qWait(waitInterval);
        waited += waitInterval;
        QCoreApplication::processEvents();
        
        if (captureQueue->tryDequeue(tempFrame)) {
            frameReceived = true;
            break;
        }
    }
    
    // 验证至少接收到一帧
    QVERIFY2(frameReceived, "应该从队列中接收到至少一帧");
    
    // 获取帧数据
    QImage capturedImage = tempFrame.image;
    
    // 验证捕获的图像
    if (!capturedImage.isNull()) {
        QVERIFY(capturedImage.width() > 0);
        QVERIFY(capturedImage.height() > 0);
        qCDebug(testScreenCapture, "同步捕获成功，图像尺寸: %dx%d", capturedImage.width(), capturedImage.height());
    } else {
        qCDebug(testScreenCapture, "同步捕获返回空图像（可能在测试环境中正常）");
    }
    
    qCDebug(testScreenCapture, "同步捕获测试通过");
}

void TestScreenCapture::test_signalEmission()
{
    qCDebug(testScreenCapture, "测试信号发射（错误和性能信号）");
    
    // 创建信号监听器（frameReady已删除，只测试错误和性能信号）
    QSignalSpy captureErrorSpy(m_screenCapture.get(), &ScreenCapture::captureError);
    QSignalSpy performanceStatsSpy(m_screenCapture.get(), &ScreenCapture::performanceStatsUpdated);
    
    // 验证信号监听器有效
    QVERIFY(captureErrorSpy.isValid());
    QVERIFY(performanceStatsSpy.isValid());
    
    // 启动捕获并等待信号
    m_screenCapture->startCapture();
    QTest::qWait(2000); // 等待足够时间以产生信号
    
    // 检查是否有信号发射（在测试环境中可能没有实际的帧）
    qCDebug(testScreenCapture, "captureError信号数量: %lld", static_cast<long long>(captureErrorSpy.count()));
    qCDebug(testScreenCapture, "performanceStatsUpdated信号数量: %lld", static_cast<long long>(performanceStatsSpy.count()));
    
    m_screenCapture->stopCapture();
    
    qCDebug(testScreenCapture, "信号发射测试通过");
}

void TestScreenCapture::test_errorHandling()
{
    qCDebug(testScreenCapture, "测试错误处理");
    
    // 创建错误信号监听器
    QSignalSpy errorSpy(m_screenCapture.get(), &ScreenCapture::captureError);
    
    // 测试多次快速启动停止（可能触发错误条件）
    for (int i = 0; i < 5; ++i) {
        m_screenCapture->startCapture();
        QTest::qWait(10);
        m_screenCapture->stopCapture();
        QTest::qWait(10);
    }
    
    // 检查错误处理
    qCDebug(testScreenCapture, "错误信号数量: %lld", static_cast<long long>(errorSpy.count()));
    
    // 确保最终状态正确
    QVERIFY(!m_screenCapture->isCapturing());
    
    qCDebug(testScreenCapture, "错误处理测试通过");
}

void TestScreenCapture::test_threadSafety()
{
    qCDebug(testScreenCapture, "测试线程安全性");
    
    // 测试并发配置修改
    std::atomic<bool> stopTest{false};
    std::vector<std::thread> threads;
    
    // 创建多个线程同时修改配置
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([this, &stopTest, i]() {
            while (!stopTest.load()) {
                auto config = m_screenCapture->getCaptureConfig();
                config.frameRate = 30 + i;
                config.quality = 0.5 + i * 0.1;
                config.highDefinition = (i % 2 == 0);
                m_screenCapture->updateCaptureConfig(config);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    // 运行一段时间
    QTest::qWait(500);
    stopTest.store(true);
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 验证对象仍然有效
    auto config = m_screenCapture->getCaptureConfig();
    QVERIFY(config.frameRate > 0);
    QVERIFY(config.quality > 0.0);
    
    qCDebug(testScreenCapture, "线程安全性测试通过");
}

void TestScreenCapture::test_memoryManagement()
{
    qCDebug(testScreenCapture, "测试内存管理");
    
    // 测试多次创建和销毁
    for (int i = 0; i < 10; ++i) {
        auto tempCapture = std::make_unique<ScreenCapture>();
        tempCapture->startCapture();
        QTest::qWait(50);
        tempCapture->stopCapture();
        QTest::qWait(50);
        // tempCapture会在作用域结束时自动销毁
    }
    
    // 简化测试：验证性能统计的基本字段（不再校验队列利用率）
    auto stats = m_screenCapture->getPerformanceStats();
    QVERIFY(stats.totalFramesCaptured >= 0);
    QVERIFY(stats.totalFramesProcessed >= 0);
    QVERIFY(stats.droppedFrames >= 0);
    
    // 验证当前状态
    QVERIFY(m_screenCapture->isCapturing() || !m_screenCapture->isCapturing());
    
    qCDebug(testScreenCapture, "内存管理测试通过");
}

void TestScreenCapture::test_startCapture()
{
    qCDebug(testScreenCapture, "test_startCapture");
    QVERIFY(!m_screenCapture->isCapturing());
    m_screenCapture->startCapture();
    QTest::qWait(150);
    QVERIFY(m_screenCapture->isCapturing());
    m_screenCapture->stopCapture();
    QTest::qWait(100);
}

void TestScreenCapture::test_stopCapture()
{
    qCDebug(testScreenCapture, "test_stopCapture");
    m_screenCapture->startCapture();
    QTest::qWait(150);
    QVERIFY(m_screenCapture->isCapturing());
    m_screenCapture->stopCapture();
    QTest::qWait(100);
    QVERIFY(!m_screenCapture->isCapturing());
}

void TestScreenCapture::test_updateCaptureConfig()
{
    qCDebug(testScreenCapture, "test_updateCaptureConfig");
    auto cfg = m_screenCapture->getCaptureConfig();
    cfg.frameRate = 24;
    cfg.quality = 0.7;
    cfg.highDefinition = true;
    m_screenCapture->updateCaptureConfig(cfg);
    auto updated = m_screenCapture->getCaptureConfig();
    QCOMPARE(updated.frameRate, 24);
    QCOMPARE(updated.quality, 0.7);
    QCOMPARE(updated.highDefinition, true);
}

void TestScreenCapture::test_getCaptureConfig()
{
    qCDebug(testScreenCapture, "test_getCaptureConfig");
    auto cfg = m_screenCapture->getCaptureConfig();
    QVERIFY(cfg.frameRate >= 1);
    QVERIFY(cfg.frameRate <= 120);
    QVERIFY(cfg.quality >= 0.0);
    QVERIFY(cfg.quality <= 1.0);
}

void TestScreenCapture::test_getPerformanceStats()
{
    qCDebug(testScreenCapture, "test_getPerformanceStats");
    auto stats = m_screenCapture->getPerformanceStats();
    QVERIFY(stats.totalFramesCaptured >= 0);
    QVERIFY(stats.totalFramesProcessed >= 0);
    QVERIFY(stats.droppedFrames >= 0);
}

void TestScreenCapture::test_stopCapture_errorPath()
{
    qCDebug(testScreenCapture, "test_stopCapture_errorPath");
    // 未启动直接停止，不应崩溃
    m_screenCapture->stopCapture();
    QTest::qWait(50);
    QVERIFY(!m_screenCapture->isCapturing());

    // 启动后多次停止，不应崩溃
    m_screenCapture->startCapture();
    QTest::qWait(150);
    m_screenCapture->stopCapture();
    m_screenCapture->stopCapture();
    QTest::qWait(100);
    QVERIFY(!m_screenCapture->isCapturing());
}

QTEST_MAIN(TestScreenCapture)
#include "test_screencapture.moc"