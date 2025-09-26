#include <QtTest/QtTest>
#include <QtCore/QTemporaryDir>
#include <QtCore/QStandardPaths>
#include "../src/server/dataprocessing/StorageManager.h"

/**
 * @brief 存储管理器测试类
 * 
 * 测试StorageManager的各种功能，包括存储、检索、清理等
 */
class TestStorageManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 测试存储管理器功能
    void test_initialization();
    void test_storeAndRetrieveFrame();
    void test_storageStatistics();
    void test_frameIdRetrieval();
    void test_performanceDataCollection();
    void test_errorDataCollection();
    void test_storageCleanup();
    void test_configurationUpdate();

private:
    StorageManager* m_storageManager;
    QTemporaryDir* m_tempDir;
    StorageManager::StorageConfig m_testConfig;
    
    // 辅助方法
    DataRecord createTestRecord(const QString& id, bool isLarge = false);
};

void TestStorageManager::initTestCase()
{
    qDebug() << "开始存储管理器测试";
}

void TestStorageManager::cleanupTestCase()
{
    qDebug() << "存储管理器测试完成";
}

void TestStorageManager::init()
{
    // 创建临时目录
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
    
    // 创建存储管理器
    m_storageManager = new StorageManager();
    
    // 设置测试配置
    m_testConfig.policy = StorageManager::StoragePolicy::FullSession;
    m_testConfig.maxStorageMB = 100;
    m_testConfig.keyFrameIntervalSec = 5;
    m_testConfig.recentFrameCount = 10;
    m_testConfig.retentionDays = 1;
    m_testConfig.compressStorage = false; // 简化测试
    m_testConfig.enableDiagnostics = true;
    m_testConfig.storageBasePath = m_tempDir->path();
    
    QVERIFY(m_storageManager != nullptr);
}

void TestStorageManager::cleanup()
{
    if (m_storageManager) {
        delete m_storageManager;
        m_storageManager = nullptr;
    }
    
    if (m_tempDir) {
        delete m_tempDir;
        m_tempDir = nullptr;
    }
}

void TestStorageManager::test_initialization()
{
    // 测试初始化
    bool success = m_storageManager->initialize(m_testConfig);
    QVERIFY2(success, "存储管理器初始化应该成功");
    
    // 验证配置
    auto config = m_storageManager->getCurrentConfig();
    QCOMPARE(config.policy, StorageManager::StoragePolicy::FullSession);
    QCOMPARE(config.maxStorageMB, 100);
    QCOMPARE(config.storageBasePath, m_tempDir->path());
    
    qDebug() << "存储管理器初始化测试通过";
}

void TestStorageManager::test_storeAndRetrieveFrame()
{
    // 初始化存储管理器
    QVERIFY(m_storageManager->initialize(m_testConfig));
    
    // 创建测试数据记录
    DataRecord testRecord = createTestRecord("test-frame-001");
    
    // 存储帧数据
    bool storeSuccess = m_storageManager->storeFrame(testRecord, true);
    QVERIFY2(storeSuccess, "存储帧数据应该成功");
    
    // 检索帧数据
    DataRecord retrievedRecord;
    bool retrieveSuccess = m_storageManager->retrieveFrame("test-frame-001", retrievedRecord);
    QVERIFY2(retrieveSuccess, "检索帧数据应该成功");
    
    // 验证数据一致性
    QCOMPARE(retrievedRecord.id, testRecord.id);
    QCOMPARE(retrievedRecord.mimeType, testRecord.mimeType);
    QCOMPARE(retrievedRecord.payload, testRecord.payload);
    QCOMPARE(retrievedRecord.checksum, testRecord.checksum);
    
    qDebug() << "帧存储和检索测试通过";
}

void TestStorageManager::test_storageStatistics()
{
    // 初始化存储管理器
    QVERIFY(m_storageManager->initialize(m_testConfig));
    
    // 获取初始统计信息
    StorageStats initialStats = m_storageManager->getStorageStatistics();
    QCOMPARE(initialStats.totalStoredFrames, 0);
    
    // 存储几个帧
    for (int i = 0; i < 5; ++i) {
        DataRecord record = createTestRecord(QString("frame-%1").arg(i));
        bool isKeyFrame = (i % 2 == 0); // 偶数帧为关键帧
        QVERIFY(m_storageManager->storeFrame(record, isKeyFrame));
    }
    
    // 获取更新后的统计信息
    StorageStats updatedStats = m_storageManager->getStorageStatistics();
    QCOMPARE(updatedStats.totalStoredFrames, 5);
    QCOMPARE(updatedStats.keyFrameCount, 3); // 0, 2, 4
    QCOMPARE(updatedStats.deltaFrameCount, 2); // 1, 3
    QVERIFY(updatedStats.averageFrameSize > 0);
    
    qDebug() << "存储统计测试通过，总帧数:" << updatedStats.totalStoredFrames;
}

void TestStorageManager::test_frameIdRetrieval()
{
    // 初始化存储管理器
    QVERIFY(m_storageManager->initialize(m_testConfig));
    
    QDateTime startTime = QDateTime::currentDateTime();
    
    // 存储一些帧
    QStringList storedIds;
    for (int i = 0; i < 3; ++i) {
        QString frameId = QString("time-frame-%1").arg(i);
        DataRecord record = createTestRecord(frameId);
        QVERIFY(m_storageManager->storeFrame(record, false));
        storedIds.append(frameId);
    }
    
    QDateTime endTime = QDateTime::currentDateTime();
    
    // 检索指定时间范围内的帧ID
    QStringList retrievedIds = m_storageManager->getStoredFrameIds(startTime, endTime);
    
    // 验证检索结果
    QCOMPARE(retrievedIds.size(), 3);
    for (const QString& id : storedIds) {
        QVERIFY2(retrievedIds.contains(id), qPrintable(QString("应该包含帧ID: %1").arg(id)));
    }
    
    qDebug() << "帧ID检索测试通过，检索到" << retrievedIds.size() << "个帧";
}

void TestStorageManager::test_performanceDataCollection()
{
    // 初始化存储管理器
    QVERIFY(m_storageManager->initialize(m_testConfig));
    
    // 收集性能数据
    QJsonObject metadata;
    metadata["test_data"] = "performance_test";
    
    m_storageManager->collectPerformanceData("test_operation", 150, metadata);
    m_storageManager->collectPerformanceData("another_operation", 75);
    
    // 生成性能报告
    QDateTime from = QDateTime::currentDateTime().addSecs(-60);
    QDateTime to = QDateTime::currentDateTime().addSecs(60);
    
    QJsonObject report = m_storageManager->generatePerformanceReport(from, to);
    
    // 验证报告结构
    QVERIFY(report.contains("type"));
    QCOMPARE(report["type"].toString(), QString("performance_report"));
    QVERIFY(report.contains("statistics"));
    
    qDebug() << "性能数据收集测试通过";
}

void TestStorageManager::test_errorDataCollection()
{
    // 初始化存储管理器
    QVERIFY(m_storageManager->initialize(m_testConfig));
    
    // 收集错误数据
    m_storageManager->collectErrorData("测试错误", "测试上下文", "warning");
    m_storageManager->collectErrorData("严重错误", "关键操作", "critical");
    
    // 生成错误报告
    QDateTime from = QDateTime::currentDateTime().addSecs(-60);
    QDateTime to = QDateTime::currentDateTime().addSecs(60);
    
    QJsonObject report = m_storageManager->generateErrorReport(from, to);
    
    // 验证报告结构
    QVERIFY(report.contains("type"));
    QCOMPARE(report["type"].toString(), QString("error_report"));
    QVERIFY(report.contains("statistics"));
    
    qDebug() << "错误数据收集测试通过";
}

void TestStorageManager::test_storageCleanup()
{
    // 初始化存储管理器
    QVERIFY(m_storageManager->initialize(m_testConfig));
    
    // 存储一些帧
    for (int i = 0; i < 5; ++i) {
        DataRecord record = createTestRecord(QString("cleanup-frame-%1").arg(i));
        QVERIFY(m_storageManager->storeFrame(record, true));
    }
    
    // 获取清理前的统计信息
    StorageStats beforeCleanup = m_storageManager->getStorageStatistics();
    QCOMPARE(beforeCleanup.totalStoredFrames, 5);
    
    // 执行清理（由于保留期为1天，正常情况下不会删除刚创建的文件）
    m_storageManager->cleanupExpiredData();
    
    // 验证清理后的统计信息
    StorageStats afterCleanup = m_storageManager->getStorageStatistics();
    // 由于文件刚创建，不应该被清理
    QCOMPARE(afterCleanup.totalStoredFrames, 5);
    
    qDebug() << "存储清理测试通过";
}

void TestStorageManager::test_configurationUpdate()
{
    // 初始化存储管理器
    QVERIFY(m_storageManager->initialize(m_testConfig));
    
    // 更新配置
    StorageManager::StorageConfig newConfig = m_testConfig;
    newConfig.maxStorageMB = 200;
    newConfig.retentionDays = 14;
    newConfig.policy = StorageManager::StoragePolicy::KeyFramesOnly;
    
    m_storageManager->updateConfig(newConfig);
    
    // 验证配置更新
    auto updatedConfig = m_storageManager->getCurrentConfig();
    QCOMPARE(updatedConfig.maxStorageMB, 200);
    QCOMPARE(updatedConfig.retentionDays, 14);
    QCOMPARE(updatedConfig.policy, StorageManager::StoragePolicy::KeyFramesOnly);
    
    qDebug() << "配置更新测试通过";
}

DataRecord TestStorageManager::createTestRecord(const QString& id, bool isLarge)
{
    DataRecord record;
    record.id = id;
    record.timestamp = QDateTime::currentDateTimeUtc();
    record.mimeType = "image/png";
    
    // 创建测试数据
    int dataSize = isLarge ? 1024 * 100 : 1024; // 100KB 或 1KB
    record.payload = QByteArray(dataSize, 'T'); // 填充测试数据
    
    record.size = QSize(640, 480);
    record.checksum = qHash(record.payload);
    
    return record;
}

QTEST_MAIN(TestStorageManager)
#include "test_storagemanager.moc"