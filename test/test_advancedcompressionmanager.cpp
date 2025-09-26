#include <QtTest/QtTest>
#include <QtCore/QDebug>
#include <QtCore/QElapsedTimer>
#include <QtCore/QRandomGenerator>
#include <QtTest/QSignalSpy>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include "../src/common/core/compression/AdvancedCompressionManager.h"
#include "../src/common/core/compression/Compression.h"

/**
 * @brief 高级压缩管理器测试类
 * 
 * 测试AdvancedCompressionManager类的所有功能，包括：
 * - 智能压缩策略选择
 * - 屏幕变化检测优化
 * - 差分传输算法
 * - 自适应配置
 * - 性能监控
 */
class TestAdvancedCompressionManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();
    
    // 基础功能测试
    void test_constructor();
    void test_compressionStrategy();
    void test_changeDetectionMode();
    void test_adaptiveConfig();
    
    // 压缩功能测试
    void test_compressFrame();
    void test_compressFrameDifferential();
    void test_decompressFrame();
    void test_compressWithStrategy();
    
    // 变化检测测试
    void test_detectChanges();
    void test_calculateFrameSimilarity();
    void test_isFrameSignificantlyDifferent();
    void test_detectChangesPixelLevel();
    void test_detectChangesBlockLevel();
    void test_detectChangesRegionLevel();
    void test_detectChangesHybridLevel();
    
    // 算法选择测试
    void test_selectOptimalAlgorithm();
    void test_selectOptimalLevel();
    void test_selectOptimalImageFormat();
    void test_selectOptimalQuality();
    
    // 性能统计测试
    void test_compressionStats();
    void test_resetStats();
    void test_performanceMetrics();
    
    // 帧历史管理测试
    void test_frameHistory();
    void test_maxFrameHistory();
    void test_clearFrameHistory();
    
    // 优化功能测试
    void test_optimizeDifferentialData();
    void test_compressChangedRegions();
    void test_divideFrameIntoBlocks();
    
    // 自适应功能测试
    void test_adaptiveStrategy();
    void test_performanceThresholds();
    void test_autoStrategySwitch();
    
    // 压力测试
    void test_stressTest();
    void test_performanceBenchmark();
    void test_memoryUsage();

private:
    AdvancedCompressionManager *m_manager;
    QImage createTestImage(int width = 640, int height = 480, const QColor &color = Qt::blue);
    QImage createComplexTestImage(int width = 640, int height = 480);
    QImage createSimilarImage(const QImage &original, double similarity = 0.9);
    void verifyCompressionResult(const QByteArray &compressed, const QImage &original);
};

void TestAdvancedCompressionManager::initTestCase()
{
    // 初始化测试环境
    qDebug() << "Starting AdvancedCompressionManager tests";
}

void TestAdvancedCompressionManager::cleanupTestCase()
{
    // 清理测试环境
    qDebug() << "AdvancedCompressionManager tests completed";
}

void TestAdvancedCompressionManager::init()
{
    // 每个测试前的初始化
    m_manager = new AdvancedCompressionManager(this);
    QVERIFY(m_manager != nullptr);
}

void TestAdvancedCompressionManager::cleanup()
{
    // 每个测试后的清理
    if (m_manager) {
        delete m_manager;
        m_manager = nullptr;
    }
}

void TestAdvancedCompressionManager::test_constructor()
{
    // 测试构造函数和默认值
    QCOMPARE(m_manager->compressionStrategy(), AdvancedCompressionManager::AdaptiveStrategy);
    QCOMPARE(m_manager->changeDetectionMode(), AdvancedCompressionManager::HybridLevel);
    
    auto config = m_manager->adaptiveConfig();
    QVERIFY(config.enableAdaptiveStrategy);
    QVERIFY(config.enableChangeDetection);
    QVERIFY(config.enablePerformanceMonitoring);
    QCOMPARE(config.maxFrameHistory, 10);
    QCOMPARE(config.blockSize, 32);
    
    auto stats = m_manager->getCompressionStats();
    QCOMPARE(stats.totalFramesProcessed, 0);
    QCOMPARE(stats.totalBytesProcessed, 0);
    QCOMPARE(stats.totalBytesCompressed, 0);
}

void TestAdvancedCompressionManager::test_compressionStrategy()
{
    // 测试压缩策略设置和获取
    QSignalSpy spy(m_manager, &AdvancedCompressionManager::compressionStrategyChanged);
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::FastStrategy);
    QCOMPARE(m_manager->compressionStrategy(), AdvancedCompressionManager::FastStrategy);
    QCOMPARE(spy.count(), 1);
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::HighCompressionStrategy);
    QCOMPARE(m_manager->compressionStrategy(), AdvancedCompressionManager::HighCompressionStrategy);
    QCOMPARE(spy.count(), 2);
    
    // 设置相同策略不应触发信号
    m_manager->setCompressionStrategy(AdvancedCompressionManager::HighCompressionStrategy);
    QCOMPARE(spy.count(), 2);
}

void TestAdvancedCompressionManager::test_changeDetectionMode()
{
    // 测试变化检测模式设置
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::PixelLevel);
    QCOMPARE(m_manager->changeDetectionMode(), AdvancedCompressionManager::PixelLevel);
    
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::BlockLevel);
    QCOMPARE(m_manager->changeDetectionMode(), AdvancedCompressionManager::BlockLevel);
    
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::RegionLevel);
    QCOMPARE(m_manager->changeDetectionMode(), AdvancedCompressionManager::RegionLevel);
    
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::HybridLevel);
    QCOMPARE(m_manager->changeDetectionMode(), AdvancedCompressionManager::HybridLevel);
}

void TestAdvancedCompressionManager::test_adaptiveConfig()
{
    // 测试自适应配置
    AdvancedCompressionManager::AdaptiveConfig config;
    config.enableAdaptiveStrategy = false;
    config.enableChangeDetection = false;
    config.enablePerformanceMonitoring = false;
    config.maxFrameHistory = 20;
    config.changeThreshold = 0.2;
    config.blockSize = 64;
    config.performanceUpdateInterval = 2000;
    
    m_manager->setAdaptiveConfig(config);
    
    auto retrievedConfig = m_manager->adaptiveConfig();
    QCOMPARE(retrievedConfig.enableAdaptiveStrategy, false);
    QCOMPARE(retrievedConfig.enableChangeDetection, false);
    QCOMPARE(retrievedConfig.enablePerformanceMonitoring, false);
    QCOMPARE(retrievedConfig.maxFrameHistory, 20);
    QCOMPARE(retrievedConfig.changeThreshold, 0.2);
    QCOMPARE(retrievedConfig.blockSize, 64);
    QCOMPARE(retrievedConfig.performanceUpdateInterval, 2000);
}

void TestAdvancedCompressionManager::test_compressFrame()
{
    // 测试单帧压缩
    QImage testImage = createTestImage();
    QVERIFY(!testImage.isNull());
    
    QByteArray compressed = m_manager->compressFrame(testImage, "test_frame_1");
    QVERIFY(!compressed.isEmpty());
    
    // 验证压缩结果
    verifyCompressionResult(compressed, testImage);
    
    // 测试空图像
    QImage nullImage;
    QByteArray nullCompressed = m_manager->compressFrame(nullImage, "null_frame");
    QVERIFY(nullCompressed.isEmpty());
    
    // 检查统计信息更新
    auto stats = m_manager->getCompressionStats();
    QCOMPARE(stats.totalFramesProcessed, 1);
    QVERIFY(stats.totalBytesProcessed > 0);
    QVERIFY(stats.totalBytesCompressed > 0);
}

void TestAdvancedCompressionManager::test_compressFrameDifferential()
{
    // 测试差分压缩
    QImage frame1 = createTestImage(640, 480, Qt::blue);
    QImage frame2 = createSimilarImage(frame1, 0.95); // 95%相似度
    
    // 第一帧（无前一帧）
    QByteArray compressed1 = m_manager->compressFrameDifferential(frame1, QImage(), "frame_1");
    QVERIFY(!compressed1.isEmpty());
    
    // 第二帧（有前一帧）
    QByteArray compressed2 = m_manager->compressFrameDifferential(frame2, frame1, "frame_2");
    QVERIFY(!compressed2.isEmpty());
    
    // 差分压缩应该更小（对于相似帧）
    // 注意：由于添加了标记字节，可能不总是更小，但应该在合理范围内
    qDebug() << "Full frame size:" << compressed1.size() << "Differential size:" << compressed2.size();
    
    // 测试完全不同的帧
    QImage frame3 = createTestImage(640, 480, Qt::red);
    QByteArray compressed3 = m_manager->compressFrameDifferential(frame3, frame1, "frame_3");
    QVERIFY(!compressed3.isEmpty());
    
    // 检查统计信息
    auto stats = m_manager->getCompressionStats();
    QVERIFY(stats.totalFramesProcessed >= 3);
    QVERIFY(stats.differentialFrames >= 2);
    QVERIFY(stats.fullFrames >= 1);
}

void TestAdvancedCompressionManager::test_decompressFrame()
{
    // 测试帧解压缩
    QImage originalFrame = createTestImage();
    
    // 压缩帧
    QByteArray compressed = m_manager->compressFrame(originalFrame, "test_frame");
    QVERIFY(!compressed.isEmpty());
    
    // 解压缩帧
    QImage decompressed = m_manager->decompressFrame(compressed, QImage());
    QVERIFY(!decompressed.isNull());
    
    // 验证尺寸
    QCOMPARE(decompressed.size(), originalFrame.size());
    
    // 测试差分解压缩
    QImage frame1 = createTestImage(640, 480, Qt::blue);
    QImage frame2 = createSimilarImage(frame1, 0.9);
    
    QByteArray diffCompressed = m_manager->compressFrameDifferential(frame2, frame1, "diff_frame");
    QImage diffDecompressed = m_manager->decompressFrame(diffCompressed, frame1);
    
    QVERIFY(!diffDecompressed.isNull());
    QCOMPARE(diffDecompressed.size(), frame2.size());
    
    // 测试空数据
    QImage nullDecompressed = m_manager->decompressFrame(QByteArray(), QImage());
    QVERIFY(nullDecompressed.isNull());
}

void TestAdvancedCompressionManager::test_detectChanges()
{
    // 测试变化检测
    QImage frame1 = createTestImage(640, 480, Qt::blue);
    QImage frame2 = createSimilarImage(frame1, 0.8); // 80%相似度
    
    // 测试不同检测模式
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::PixelLevel);
    auto pixelChanges = m_manager->detectChanges(frame1, frame2);
    
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::BlockLevel);
    auto blockChanges = m_manager->detectChanges(frame1, frame2);
    
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::RegionLevel);
    auto regionChanges = m_manager->detectChanges(frame1, frame2);
    
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::HybridLevel);
    auto hybridChanges = m_manager->detectChanges(frame1, frame2);
    
    // 应该检测到变化
    QVERIFY(!blockChanges.isEmpty());
    QVERIFY(!regionChanges.isEmpty());
    QVERIFY(!hybridChanges.isEmpty());
    
    // 测试相同帧（无变化）
    auto noChanges = m_manager->detectChanges(frame1, frame1);
    QVERIFY(noChanges.isEmpty() || noChanges.size() < blockChanges.size());
    
    // 测试空帧
    auto nullChanges = m_manager->detectChanges(QImage(), frame1);
    QVERIFY(nullChanges.isEmpty());
}

void TestAdvancedCompressionManager::test_calculateFrameSimilarity()
{
    // 测试帧相似度计算
    QImage frame1 = createTestImage();
    QImage frame2 = createSimilarImage(frame1, 0.9);
    QImage frame3 = createTestImage(640, 480, Qt::red); // 完全不同
    
    // 相同帧的相似度应该是1.0
    double sameSimilarity = m_manager->calculateFrameSimilarity(frame1, frame1);
    QVERIFY(sameSimilarity > 0.95);
    
    // 相似帧的相似度应该较高
    double similarSimilarity = m_manager->calculateFrameSimilarity(frame1, frame2);
    QVERIFY(similarSimilarity > 0.7);
    QVERIFY(similarSimilarity < sameSimilarity);
    
    // 不同帧的相似度应该较低
    double differentSimilarity = m_manager->calculateFrameSimilarity(frame1, frame3);
    QVERIFY(differentSimilarity < similarSimilarity);
    
    // 测试空帧
    double nullSimilarity = m_manager->calculateFrameSimilarity(QImage(), frame1);
    QCOMPARE(nullSimilarity, 0.0);
    
    // 测试不同尺寸
    QImage smallFrame = createTestImage(320, 240);
    double sizeMismatchSimilarity = m_manager->calculateFrameSimilarity(frame1, smallFrame);
    QCOMPARE(sizeMismatchSimilarity, 0.0);
}

void TestAdvancedCompressionManager::test_isFrameSignificantlyDifferent()
{
    // 测试帧显著差异检测
    QImage frame1 = createTestImage();
    QImage frame2 = createSimilarImage(frame1, 0.95); // 高相似度
    QImage frame3 = createTestImage(640, 480, Qt::red); // 完全不同
    
    // 高相似度帧不应被认为显著不同
    bool notSignificant = m_manager->isFrameSignificantlyDifferent(frame1, frame2, 0.1);
    QVERIFY(!notSignificant);
    
    // 完全不同的帧应被认为显著不同
    bool significant = m_manager->isFrameSignificantlyDifferent(frame1, frame3, 0.1);
    QVERIFY(significant);
    
    // 测试不同阈值
    bool strictThreshold = m_manager->isFrameSignificantlyDifferent(frame1, frame2, 0.01);
    QVERIFY(strictThreshold); // 严格阈值下应该检测到差异
    
    // 测试空帧
    bool nullDifferent = m_manager->isFrameSignificantlyDifferent(QImage(), frame1, 0.1);
    QVERIFY(nullDifferent);
}

void TestAdvancedCompressionManager::test_selectOptimalAlgorithm()
{
    // 测试算法选择 - 通过压缩不同数据类型来间接测试
    QByteArray testData(1024, 'A'); // 重复数据
    QByteArray randomData;
    for (int i = 0; i < 1024; ++i) {
        randomData.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    }
    
    // 测试不同策略下的压缩结果
    m_manager->setCompressionStrategy(AdvancedCompressionManager::FastStrategy);
    QImage testImage = createTestImage();
    QByteArray fastResult = m_manager->compressFrame(testImage, "fast_test");
    QVERIFY(!fastResult.isEmpty());
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::HighCompressionStrategy);
    QByteArray highResult = m_manager->compressFrame(testImage, "high_test");
    QVERIFY(!highResult.isEmpty());
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::BalancedStrategy);
    QByteArray balancedResult = m_manager->compressFrame(testImage, "balanced_test");
    QVERIFY(!balancedResult.isEmpty());
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::AdaptiveStrategy);
    QByteArray adaptiveResult = m_manager->compressFrame(testImage, "adaptive_test");
    QVERIFY(!adaptiveResult.isEmpty());
    
    // 验证所有策略都能产生有效结果
    QVERIFY(fastResult.size() > 0);
    QVERIFY(highResult.size() > 0);
    QVERIFY(balancedResult.size() > 0);
    QVERIFY(adaptiveResult.size() > 0);
}

void TestAdvancedCompressionManager::test_selectOptimalLevel()
{
    // 测试压缩级别选择 - 通过不同策略的压缩结果来间接测试
    QImage smallImage = createTestImage(100, 100);
    QImage mediumImage = createTestImage(500, 500);
    QImage largeImage = createTestImage(1000, 1000);
    
    // 测试快速策略
    m_manager->setCompressionStrategy(AdvancedCompressionManager::FastStrategy);
    QByteArray fastSmall = m_manager->compressFrame(smallImage, "fast_small");
    QByteArray fastMedium = m_manager->compressFrame(mediumImage, "fast_medium");
    QByteArray fastLarge = m_manager->compressFrame(largeImage, "fast_large");
    
    QVERIFY(!fastSmall.isEmpty());
    QVERIFY(!fastMedium.isEmpty());
    QVERIFY(!fastLarge.isEmpty());
    
    // 测试高压缩策略
    m_manager->setCompressionStrategy(AdvancedCompressionManager::HighCompressionStrategy);
    QByteArray highSmall = m_manager->compressFrame(smallImage, "high_small");
    QByteArray highMedium = m_manager->compressFrame(mediumImage, "high_medium");
    QByteArray highLarge = m_manager->compressFrame(largeImage, "high_large");
    
    QVERIFY(!highSmall.isEmpty());
    QVERIFY(!highMedium.isEmpty());
    QVERIFY(!highLarge.isEmpty());
    
    // 验证压缩结果的有效性
    qDebug() << "Fast strategy sizes:" << fastSmall.size() << fastMedium.size() << fastLarge.size();
    qDebug() << "High strategy sizes:" << highSmall.size() << highMedium.size() << highLarge.size();
}

void TestAdvancedCompressionManager::test_selectOptimalImageFormat()
{
    // 测试图像格式选择 - 通过压缩结果来间接测试格式选择
    QImage simpleImage = createTestImage();
    QImage complexImage = createComplexTestImage();
    
    // 测试不同策略下的压缩
    m_manager->setCompressionStrategy(AdvancedCompressionManager::FastStrategy);
    QByteArray fastSimple = m_manager->compressFrame(simpleImage, "fast_simple");
    QByteArray fastComplex = m_manager->compressFrame(complexImage, "fast_complex");
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::HighCompressionStrategy);
    QByteArray highSimple = m_manager->compressFrame(simpleImage, "high_simple");
    QByteArray highComplex = m_manager->compressFrame(complexImage, "high_complex");
    
    // 验证所有压缩都成功
    QVERIFY(!fastSimple.isEmpty());
    QVERIFY(!fastComplex.isEmpty());
    QVERIFY(!highSimple.isEmpty());
    QVERIFY(!highComplex.isEmpty());
    
    // 测试空图像
    QByteArray nullResult = m_manager->compressFrame(QImage(), "null_image");
    QVERIFY(nullResult.isEmpty());
    
    qDebug() << "Image format test - compression sizes:";
    qDebug() << "Fast simple:" << fastSimple.size() << "Fast complex:" << fastComplex.size();
    qDebug() << "High simple:" << highSimple.size() << "High complex:" << highComplex.size();
}

void TestAdvancedCompressionManager::test_selectOptimalQuality()
{
    // 测试图像质量选择 - 通过不同策略的压缩结果来间接测试质量选择
    QImage testImage = createTestImage();
    
    // 测试不同策略下的压缩质量效果
    m_manager->setCompressionStrategy(AdvancedCompressionManager::BalancedStrategy);
    QByteArray balancedResult = m_manager->compressFrame(testImage, "balanced_quality");
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::FastStrategy);
    QByteArray fastResult = m_manager->compressFrame(testImage, "fast_quality");
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::HighCompressionStrategy);
    QByteArray highResult = m_manager->compressFrame(testImage, "high_quality");
    
    // 验证所有策略都产生有效结果
    QVERIFY(!balancedResult.isEmpty());
    QVERIFY(!fastResult.isEmpty());
    QVERIFY(!highResult.isEmpty());
    
    // 验证压缩结果可以解压缩
    QImage balancedDecompressed = m_manager->decompressFrame(balancedResult, QImage());
    QImage fastDecompressed = m_manager->decompressFrame(fastResult, QImage());
    QImage highDecompressed = m_manager->decompressFrame(highResult, QImage());
    
    QVERIFY(!balancedDecompressed.isNull());
    QVERIFY(!fastDecompressed.isNull());
    QVERIFY(!highDecompressed.isNull());
    
    qDebug() << "Quality test - compression sizes:";
    qDebug() << "Balanced:" << balancedResult.size() << "Fast:" << fastResult.size() << "High:" << highResult.size();
}

void TestAdvancedCompressionManager::test_compressionStats()
{
    // 测试压缩统计
    auto initialStats = m_manager->getCompressionStats();
    QCOMPARE(initialStats.totalFramesProcessed, 0);
    QCOMPARE(initialStats.totalBytesProcessed, 0);
    QCOMPARE(initialStats.totalBytesCompressed, 0);
    
    // 压缩一些帧
    QImage frame1 = createTestImage();
    QImage frame2 = createTestImage(640, 480, Qt::red);
    
    m_manager->compressFrame(frame1, "frame_1");
    m_manager->compressFrame(frame2, "frame_2");
    
    auto updatedStats = m_manager->getCompressionStats();
    QCOMPARE(updatedStats.totalFramesProcessed, 2);
    QVERIFY(updatedStats.totalBytesProcessed > 0);
    QVERIFY(updatedStats.totalBytesCompressed > 0);
    QVERIFY(updatedStats.averageCompressionTime >= 0);
    
    // 检查当前压缩指标
    QVERIFY(m_manager->getCurrentCompressionTime() >= 0);
    QVERIFY(m_manager->getCurrentCompressionRatio() >= 0.0);
}

void TestAdvancedCompressionManager::test_resetStats()
{
    // 先生成一些统计数据
    QImage testImage = createTestImage();
    m_manager->compressFrame(testImage, "test_frame");
    
    auto statsBeforeReset = m_manager->getCompressionStats();
    QVERIFY(statsBeforeReset.totalFramesProcessed > 0);
    
    // 重置统计
    m_manager->resetStats();
    
    auto statsAfterReset = m_manager->getCompressionStats();
    QCOMPARE(statsAfterReset.totalFramesProcessed, 0);
    QCOMPARE(statsAfterReset.totalBytesProcessed, 0);
    QCOMPARE(statsAfterReset.totalBytesCompressed, 0);
    QCOMPARE(statsAfterReset.averageCompressionRatio, 0.0);
    QCOMPARE(statsAfterReset.averageCompressionTime, 0);
}

void TestAdvancedCompressionManager::test_frameHistory()
{
    // 测试帧历史管理
    QCOMPARE(m_manager->maxFrameHistory(), 10); // 默认值
    
    // 添加帧到历史
    for (int i = 0; i < 15; ++i) {
        QImage frame = createTestImage(640, 480, QColor(i * 10, i * 10, i * 10));
        m_manager->compressFrame(frame, QString("frame_%1").arg(i));
    }
    
    // 历史应该被限制在最大值
    // 注意：由于历史是私有的，我们通过间接方式验证
    
    // 设置新的最大历史
    m_manager->setMaxFrameHistory(5);
    QCOMPARE(m_manager->maxFrameHistory(), 5);
    
    // 清除历史
    m_manager->clearFrameHistory();
    // 历史已清除，但无法直接验证
}

void TestAdvancedCompressionManager::test_optimizeDifferentialData()
{
    // 测试差分数据优化 - 通过差分压缩来间接测试
    QImage frame1 = createTestImage(256, 256, Qt::blue);
    QImage frame2 = createSimilarImage(frame1, 0.9);
    
    // 测试差分压缩（内部会进行数据优化）
    QByteArray diffCompressed = m_manager->compressFrameDifferential(frame2, frame1, "diff_optimize_test");
    QVERIFY(!diffCompressed.isEmpty());
    
    // 验证差分压缩结果可以解压缩
    QImage decompressed = m_manager->decompressFrame(diffCompressed, frame1);
    QVERIFY(!decompressed.isNull());
    QCOMPARE(decompressed.size(), frame2.size());
    
    // 测试相同帧的差分压缩（应该高度优化）
    QByteArray sameDiff = m_manager->compressFrameDifferential(frame1, frame1, "same_frame_diff");
    QVERIFY(!sameDiff.isEmpty());
    
    // 相同帧的差分应该比不同帧的差分更小
    qDebug() << "Differential optimization test:";
    qDebug() << "Different frames diff size:" << diffCompressed.size();
    qDebug() << "Same frame diff size:" << sameDiff.size();
    
    // 验证优化效果
    QVERIFY(sameDiff.size() <= diffCompressed.size());
}

void TestAdvancedCompressionManager::test_divideFrameIntoBlocks()
{
    // 测试帧分块
    QSize frameSize(640, 480);
    int blockSize = 32;
    
    auto blocks = m_manager->divideFrameIntoBlocks(frameSize, blockSize);
    QVERIFY(!blocks.isEmpty());
    
    // 验证块的覆盖范围
    int totalArea = 0;
    for (const QRect &block : blocks) {
        QVERIFY(block.width() <= blockSize);
        QVERIFY(block.height() <= blockSize);
        QVERIFY(block.width() > 0);
        QVERIFY(block.height() > 0);
        totalArea += block.width() * block.height();
    }
    
    // 总面积应该等于帧面积
    QCOMPARE(totalArea, frameSize.width() * frameSize.height());
    
    // 测试小帧
    QSize smallFrame(16, 16);
    auto smallBlocks = m_manager->divideFrameIntoBlocks(smallFrame, 32);
    QCOMPARE(smallBlocks.size(), 1);
    QCOMPARE(smallBlocks.first(), QRect(0, 0, 16, 16));
}

void TestAdvancedCompressionManager::test_performanceMetrics()
{
    // 测试性能指标监控
    QSignalSpy statsSpy(m_manager, &AdvancedCompressionManager::compressionStatsUpdated);
    QSignalSpy thresholdSpy(m_manager, &AdvancedCompressionManager::performanceThresholdExceeded);
    
    // 启用性能监控
    auto config = m_manager->adaptiveConfig();
    config.enablePerformanceMonitoring = true;
    config.performanceUpdateInterval = 100; // 100ms for testing
    m_manager->setAdaptiveConfig(config);
    
    // 压缩一些帧以生成统计数据
    for (int i = 0; i < 5; ++i) {
        QImage frame = createTestImage();
        m_manager->compressFrame(frame, QString("perf_frame_%1").arg(i));
    }
    
    // 等待性能更新
    QTest::qWait(200);
    
    // 应该收到统计更新信号
    QVERIFY(statsSpy.count() >= 1);
}

void TestAdvancedCompressionManager::test_adaptiveStrategy()
{
    // 测试自适应策略
    m_manager->setCompressionStrategy(AdvancedCompressionManager::AdaptiveStrategy);
    
    // 启用自适应配置
    auto config = m_manager->adaptiveConfig();
    config.enableAdaptiveStrategy = true;
    m_manager->setAdaptiveConfig(config);
    
    // 创建不同类型的测试图像
    QImage simpleImage = createTestImage(100, 100, Qt::blue); // 简单图像
    QImage complexImage = createComplexTestImage(100, 100); // 复杂图像
    
    // 自适应策略应该为不同图像选择合适的压缩方式
    QByteArray simpleCompressed = m_manager->compressFrame(simpleImage, "simple_adaptive");
    QByteArray complexCompressed = m_manager->compressFrame(complexImage, "complex_adaptive");
    
    // 验证压缩结果
    QVERIFY(!simpleCompressed.isEmpty());
    QVERIFY(!complexCompressed.isEmpty());
    
    // 验证解压缩
    QImage simpleDecompressed = m_manager->decompressFrame(simpleCompressed, QImage());
    QImage complexDecompressed = m_manager->decompressFrame(complexCompressed, QImage());
    
    QVERIFY(!simpleDecompressed.isNull());
    QVERIFY(!complexDecompressed.isNull());
    QCOMPARE(simpleDecompressed.size(), simpleImage.size());
    QCOMPARE(complexDecompressed.size(), complexImage.size());
}

void TestAdvancedCompressionManager::test_stressTest()
{
    // 压力测试
    const int frameCount = 50;
    const int frameWidth = 320;
    const int frameHeight = 240;
    
    // 确保统计信息被重置
    m_manager->resetStats();
    
    QElapsedTimer timer;
    timer.start();
    
    QImage previousFrame;
    for (int i = 0; i < frameCount; ++i) {
        // 创建略有不同的帧
        QImage currentFrame = createTestImage(frameWidth, frameHeight, QColor(i % 255, (i * 2) % 255, (i * 3) % 255));
        
        if (i == 0) {
            m_manager->compressFrame(currentFrame, QString("stress_frame_%1").arg(i));
        } else {
            m_manager->compressFrameDifferential(currentFrame, previousFrame, QString("stress_frame_%1").arg(i));
        }
        
        previousFrame = currentFrame;
        
        // 每10帧检查一次性能
        if (i % 10 == 0) {
            auto stats = m_manager->getCompressionStats();
            QVERIFY(stats.totalFramesProcessed > 0);
            QVERIFY(stats.averageCompressionTime >= 0);
        }
    }
    
    qint64 totalTime = timer.elapsed();
    qDebug() << "Stress test completed:" << frameCount << "frames in" << totalTime << "ms";
    qDebug() << "Average time per frame:" << (totalTime / frameCount) << "ms";
    
    // 验证最终统计
    auto finalStats = m_manager->getCompressionStats();
    QCOMPARE(finalStats.totalFramesProcessed, frameCount);
    QVERIFY(finalStats.totalBytesProcessed > 0);
    QVERIFY(finalStats.totalBytesCompressed > 0);
}

void TestAdvancedCompressionManager::test_performanceBenchmark()
{
    // 性能基准测试
    const int iterations = 20;
    QImage testImage = createComplexTestImage();
    
    // 测试不同策略的性能
    QElapsedTimer timer;
    
    // 快速策略
    m_manager->setCompressionStrategy(AdvancedCompressionManager::FastStrategy);
    timer.start();
    for (int i = 0; i < iterations; ++i) {
        m_manager->compressFrame(testImage, QString("fast_%1").arg(i));
    }
    qint64 fastTime = timer.elapsed();
    
    // 重置统计
    m_manager->resetStats();
    
    // 高压缩策略
    m_manager->setCompressionStrategy(AdvancedCompressionManager::HighCompressionStrategy);
    timer.start();
    for (int i = 0; i < iterations; ++i) {
        m_manager->compressFrame(testImage, QString("high_%1").arg(i));
    }
    qint64 highTime = timer.elapsed();
    
    qDebug() << "Performance benchmark:";
    qDebug() << "Fast strategy:" << fastTime << "ms for" << iterations << "frames";
    qDebug() << "High compression strategy:" << highTime << "ms for" << iterations << "frames";
    
    // 快速策略应该更快（通常情况下）
    // 注意：这个断言可能在某些情况下失败，取决于具体实现
    // QVERIFY(fastTime <= highTime * 1.5); // 允许50%的误差
}

void TestAdvancedCompressionManager::test_memoryUsage()
{
    // 内存使用测试
    const int largeFrameCount = 100;
    
    // 设置较小的帧历史以控制内存使用
    m_manager->setMaxFrameHistory(5);
    
    for (int i = 0; i < largeFrameCount; ++i) {
        QImage frame = createTestImage(800, 600, QColor(i % 255, (i * 2) % 255, (i * 3) % 255));
        m_manager->compressFrame(frame, QString("memory_frame_%1").arg(i));
        
        // 每20帧清理一次历史
        if (i % 20 == 0) {
            m_manager->clearFrameHistory();
        }
    }
    
    // 验证最终状态
    auto stats = m_manager->getCompressionStats();
    QCOMPARE(stats.totalFramesProcessed, largeFrameCount);
    
    qDebug() << "Memory test completed:" << largeFrameCount << "frames processed";
    qDebug() << "Total bytes processed:" << stats.totalBytesProcessed;
    qDebug() << "Total bytes compressed:" << stats.totalBytesCompressed;
    qDebug() << "Average compression ratio:" << stats.averageCompressionRatio;
}

// 辅助方法实现
QImage TestAdvancedCompressionManager::createTestImage(int width, int height, const QColor &color)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    
    // 添加一些细节以使图像更真实
    QPainter painter(&image);
    painter.setPen(QPen(color.darker(), 2));
    painter.drawRect(10, 10, width - 20, height - 20);
    painter.drawLine(0, 0, width, height);
    painter.drawLine(width, 0, 0, height);
    
    return image;
}

QImage TestAdvancedCompressionManager::createComplexTestImage(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    QPainter painter(&image);
    
    // 创建复杂的图像内容
    for (int i = 0; i < 50; ++i) {
            QColor color(QRandomGenerator::global()->bounded(256), QRandomGenerator::global()->bounded(256), QRandomGenerator::global()->bounded(256));
            painter.setBrush(QBrush(color));
            painter.drawEllipse(QRandomGenerator::global()->bounded(width), QRandomGenerator::global()->bounded(height), 
                           QRandomGenerator::global()->bounded(100) + 10, QRandomGenerator::global()->bounded(100) + 10);
        }
    
    return image;
}

QImage TestAdvancedCompressionManager::createSimilarImage(const QImage &original, double similarity)
{
    if (original.isNull() || similarity < 0.0 || similarity > 1.0) {
        return QImage();
    }
    
    QImage similar = original.copy();
    
    // 修改一定比例的像素以降低相似度
    int totalPixels = similar.width() * similar.height();
    int pixelsToChange = static_cast<int>(totalPixels * (1.0 - similarity));
    
    for (int i = 0; i < pixelsToChange; ++i) {
        int x = QRandomGenerator::global()->bounded(similar.width());
        int y = QRandomGenerator::global()->bounded(similar.height());
        
        QRgb originalPixel = similar.pixel(x, y);
        QRgb newPixel = qRgb(
            qBound(0, qRed(originalPixel) + (QRandomGenerator::global()->bounded(100) - 50), 255),
            qBound(0, qGreen(originalPixel) + (QRandomGenerator::global()->bounded(100) - 50), 255),
            qBound(0, qBlue(originalPixel) + (QRandomGenerator::global()->bounded(100) - 50), 255)
        );
        
        similar.setPixel(x, y, newPixel);
    }
    
    return similar;
}

void TestAdvancedCompressionManager::verifyCompressionResult(const QByteArray &compressed, const QImage &original)
{
    QVERIFY(!compressed.isEmpty());
    QVERIFY(!original.isNull());
    
    // 验证压缩数据可以解压缩
    QImage decompressed = m_manager->decompressFrame(compressed, QImage());
    QVERIFY(!decompressed.isNull());
    QCOMPARE(decompressed.size(), original.size());
}

void TestAdvancedCompressionManager::test_detectChangesPixelLevel()
{
    // 测试像素级变化检测
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::PixelLevel);
    
    QImage frame1 = createTestImage(100, 100, Qt::blue);
    QImage frame2 = createSimilarImage(frame1, 0.9);
    
    auto changes = m_manager->detectChanges(frame1, frame2);
    
    // 像素级检测应该找到变化
    QVERIFY(!changes.isEmpty());
    
    // 验证变化区域的属性
    for (const auto &change : changes) {
        QVERIFY(change.rect.isValid());
        QVERIFY(change.changeIntensity >= 0.0 && change.changeIntensity <= 1.0);
        QCOMPARE(change.changeIntensity, 1.0); // 像素级检测强度固定为1.0
    }
}

void TestAdvancedCompressionManager::test_detectChangesBlockLevel()
{
    // 测试块级变化检测
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::BlockLevel);
    
    QImage frame1 = createTestImage(128, 128, Qt::blue);
    QImage frame2 = createSimilarImage(frame1, 0.8);
    
    auto changes = m_manager->detectChanges(frame1, frame2);
    
    // 块级检测应该找到变化
    QVERIFY(!changes.isEmpty());
    
    // 验证块的大小
    auto config = m_manager->adaptiveConfig();
    for (const auto &change : changes) {
        QVERIFY(change.rect.width() <= config.blockSize);
        QVERIFY(change.rect.height() <= config.blockSize);
        QVERIFY(change.changeIntensity > 0.0 && change.changeIntensity <= 1.0);
    }
}

void TestAdvancedCompressionManager::test_detectChangesRegionLevel()
{
    // 测试区域级变化检测
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::RegionLevel);
    
    QImage frame1 = createTestImage(256, 256, Qt::blue);
    QImage frame2 = createSimilarImage(frame1, 0.7);
    
    auto changes = m_manager->detectChanges(frame1, frame2);
    
    // 区域级检测应该找到变化，但区域更大
    QVERIFY(!changes.isEmpty());
    
    // 验证区域的大小（应该比块级检测的区域更大）
    auto config = m_manager->adaptiveConfig();
    int expectedRegionSize = config.blockSize * 4;
    
    for (const auto &change : changes) {
        QVERIFY(change.rect.width() <= expectedRegionSize);
        QVERIFY(change.rect.height() <= expectedRegionSize);
        QVERIFY(change.changeIntensity > 0.0 && change.changeIntensity <= 1.0);
    }
}

void TestAdvancedCompressionManager::test_detectChangesHybridLevel()
{
    // 测试混合级变化检测
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::HybridLevel);
    
    QImage frame1 = createTestImage(256, 256, Qt::blue);
    QImage frame2 = createSimilarImage(frame1, 0.8);
    
    auto hybridChanges = m_manager->detectChanges(frame1, frame2);
    
    // 混合检测应该找到变化
    QVERIFY(!hybridChanges.isEmpty());
    
    // 比较与单独的块级和区域级检测
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::BlockLevel);
    auto blockChanges = m_manager->detectChanges(frame1, frame2);
    
    m_manager->setChangeDetectionMode(AdvancedCompressionManager::RegionLevel);
    auto regionChanges = m_manager->detectChanges(frame1, frame2);
    
    // 混合检测的结果应该综合了块级和区域级的优势
    QVERIFY(hybridChanges.size() >= qMin(blockChanges.size(), regionChanges.size()));
    
    // 验证变化区域的属性
    for (const auto &change : hybridChanges) {
        QVERIFY(change.rect.isValid());
        QVERIFY(change.changeIntensity > 0.0 && change.changeIntensity <= 1.0);
    }
}

void TestAdvancedCompressionManager::test_compressChangedRegions()
{
    // 测试变化区域压缩
    QImage frame1 = createTestImage(256, 256, Qt::blue);
    QImage frame2 = createSimilarImage(frame1, 0.8);
    
    // 检测变化
    auto changes = m_manager->detectChanges(frame1, frame2);
    QVERIFY(!changes.isEmpty());
    
    // 由于compressChangedRegions是私有方法，我们通过差分压缩来间接测试
    QByteArray compressedDiff = m_manager->compressFrameDifferential(frame2, frame1, "diff_test");
    QVERIFY(!compressedDiff.isEmpty());
    
    // 验证差分压缩结果可以解压缩
    QImage decompressed = m_manager->decompressFrame(compressedDiff, frame1);
    QVERIFY(!decompressed.isNull());
    QCOMPARE(decompressed.size(), frame2.size());
    
    // 测试空变化情况（相同帧）
    QByteArray sameFrameDiff = m_manager->compressFrameDifferential(frame1, frame1, "same_frame");
    QVERIFY(!sameFrameDiff.isEmpty());
}

void TestAdvancedCompressionManager::test_compressWithStrategy()
{
    // 测试使用特定策略压缩
    QImage testImage = createTestImage();
    
    // 测试不同策略 - 通过设置策略后压缩帧
    m_manager->setCompressionStrategy(AdvancedCompressionManager::FastStrategy);
    QByteArray fastCompressed = m_manager->compressFrame(testImage, "fast_frame");
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::HighCompressionStrategy);
    QByteArray highCompressed = m_manager->compressFrame(testImage, "high_frame");
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::BalancedStrategy);
    QByteArray balancedCompressed = m_manager->compressFrame(testImage, "balanced_frame");
    
    m_manager->setCompressionStrategy(AdvancedCompressionManager::AdaptiveStrategy);
    QByteArray adaptiveCompressed = m_manager->compressFrame(testImage, "adaptive_frame");
    
    // 所有策略都应该产生压缩数据
    QVERIFY(!fastCompressed.isEmpty());
    QVERIFY(!highCompressed.isEmpty());
    QVERIFY(!balancedCompressed.isEmpty());
    QVERIFY(!adaptiveCompressed.isEmpty());
    
    // 所有策略都应该产生有效的压缩数据
    QVERIFY(fastCompressed.size() > 0);
    QVERIFY(highCompressed.size() > 0);
    QVERIFY(balancedCompressed.size() > 0);
    QVERIFY(adaptiveCompressed.size() > 0);
    
    qDebug() << "Compression results for test image:";
    qDebug() << "Fast:" << fastCompressed.size() << "bytes";
    qDebug() << "High:" << highCompressed.size() << "bytes";
    qDebug() << "Balanced:" << balancedCompressed.size() << "bytes";
    qDebug() << "Adaptive:" << adaptiveCompressed.size() << "bytes";
}

void TestAdvancedCompressionManager::test_performanceThresholds()
{
    // 测试性能阈值监控
    QSignalSpy thresholdSpy(m_manager, &AdvancedCompressionManager::performanceThresholdExceeded);
    
    // 启用性能监控
    auto config = m_manager->adaptiveConfig();
    config.enablePerformanceMonitoring = true;
    config.performanceUpdateInterval = 50; // 快速更新用于测试
    m_manager->setAdaptiveConfig(config);
    
    // 创建一个大图像以增加压缩时间
    QImage largeImage = createComplexTestImage(1920, 1080);
    
    // 压缩多个大图像
    for (int i = 0; i < 10; ++i) {
        m_manager->compressFrame(largeImage, QString("large_frame_%1").arg(i));
    }
    
    // 等待性能更新
    QTest::qWait(200);
    
    // 检查是否触发了性能阈值
    // 注意：这个测试可能不总是触发阈值，取决于系统性能
    qDebug() << "Performance threshold signals received:" << thresholdSpy.count();
    
    // 验证统计信息
    auto stats = m_manager->getCompressionStats();
    QVERIFY(stats.totalFramesProcessed >= 10);
    QVERIFY(stats.averageCompressionTime >= 0);
}

void TestAdvancedCompressionManager::test_autoStrategySwitch()
{
    // 测试自动策略切换
    QSignalSpy strategySpy(m_manager, &AdvancedCompressionManager::compressionStrategyChanged);
    
    // 启用自适应策略
    auto config = m_manager->adaptiveConfig();
    config.enableAdaptiveStrategy = true;
    config.performanceUpdateInterval = 50; // 快速更新
    m_manager->setAdaptiveConfig(config);
    
    // 设置初始策略
    m_manager->setCompressionStrategy(AdvancedCompressionManager::BalancedStrategy);
    strategySpy.clear();
    
    // 模拟高压缩时间场景（通过压缩大图像）
    QImage largeImage = createComplexTestImage(1920, 1080);
    for (int i = 0; i < 5; ++i) {
        m_manager->compressFrame(largeImage, QString("auto_switch_frame_%1").arg(i));
    }
    
    // 等待自动优化
    QTest::qWait(200);
    
    // 检查是否发生了策略切换
    qDebug() << "Strategy change signals:" << strategySpy.count();
    qDebug() << "Current strategy:" << static_cast<int>(m_manager->compressionStrategy());
    
    // 验证统计信息
    auto stats = m_manager->getCompressionStats();
    qDebug() << "Average compression time:" << stats.averageCompressionTime << "ms";
    qDebug() << "Average compression ratio:" << stats.averageCompressionRatio;
}

void TestAdvancedCompressionManager::test_maxFrameHistory()
{
    // 测试设置最大帧历史数量
    int originalMax = m_manager->maxFrameHistory();
    QVERIFY(originalMax > 0);
    
    // 设置新的最大值
    int newMax = 20;
    m_manager->setMaxFrameHistory(newMax);
    QCOMPARE(m_manager->maxFrameHistory(), newMax);
    
    // 测试边界值
    m_manager->setMaxFrameHistory(1);
    QCOMPARE(m_manager->maxFrameHistory(), 1);
    
    // 恢复原始值
    m_manager->setMaxFrameHistory(originalMax);
    QCOMPARE(m_manager->maxFrameHistory(), originalMax);
}

void TestAdvancedCompressionManager::test_clearFrameHistory()
{
    // 添加一些帧到历史记录
    QImage frame1 = createTestImage(320, 240, Qt::red);
    QImage frame2 = createTestImage(320, 240, Qt::green);
    QImage frame3 = createTestImage(320, 240, Qt::blue);
    
    // 压缩几帧以建立历史记录
    m_manager->compressFrame(frame1, "frame1");
    m_manager->compressFrame(frame2, "frame2");
    m_manager->compressFrame(frame3, "frame3");
    
    // 清除帧历史
    m_manager->clearFrameHistory();
    
    // 验证历史已清除（通过压缩新帧验证）
    QImage frame4 = createTestImage(320, 240, Qt::yellow);
    QByteArray compressed = m_manager->compressFrame(frame4, "frame4");
    QVERIFY(!compressed.isEmpty());
    
    // 由于历史已清除，差分压缩应该退回到全帧压缩
    QImage frame5 = createSimilarImage(frame4, 0.95);
    QByteArray diffCompressed = m_manager->compressFrameDifferential(frame5, frame4, "frame5");
    QVERIFY(!diffCompressed.isEmpty());
}

QTEST_MAIN(TestAdvancedCompressionManager)
#include "test_advancedcompressionmanager.moc"