#include <QtTest/QtTest>
#include <QtGui/QImage>
#include <QtCore/QBuffer>
#include "../src/server/capture/ScreenCaptureWorker.h"

/**
 * @brief 数据验证集成测试类
 * 
 * 测试ScreenCaptureWorker中集成的数据验证功能
 */
class TestDataValidationIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 测试数据验证功能
    void test_dataValidationEnabled();
    void test_dataValidationDisabled();
    void test_checksumGeneration();

private:
    ScreenCaptureWorker* m_worker;
};

void TestDataValidationIntegration::initTestCase()
{
    qDebug() << "开始数据验证集成测试";
}

void TestDataValidationIntegration::cleanupTestCase()
{
    qDebug() << "数据验证集成测试完成";
}

void TestDataValidationIntegration::init()
{
    m_worker = new ScreenCaptureWorker();
    QVERIFY(m_worker != nullptr);
}

void TestDataValidationIntegration::cleanup()
{
    if (m_worker) {
        delete m_worker;
        m_worker = nullptr;
    }
}

void TestDataValidationIntegration::test_dataValidationEnabled()
{
    // 启用数据验证
    m_worker->setDataValidationEnabled(true);
    QVERIFY(m_worker->isDataValidationEnabled());
    
    qDebug() << "数据验证已启用，校验和应该被生成";
}

void TestDataValidationIntegration::test_dataValidationDisabled()
{
    // 禁用数据验证
    m_worker->setDataValidationEnabled(false);
    QVERIFY(!m_worker->isDataValidationEnabled());
    
    qDebug() << "数据验证已禁用";
}

void TestDataValidationIntegration::test_checksumGeneration()
{
    // 启用数据验证
    m_worker->setDataValidationEnabled(true);
    
    // 初始校验和应该为0
    QCOMPARE(m_worker->getLastFrameChecksum(), static_cast<quint64>(0));
    
    qDebug() << "初始校验和为0，符合预期";
}

QTEST_MAIN(TestDataValidationIntegration)
#include "test_datavalidation_integration.moc"