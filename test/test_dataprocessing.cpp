#include <QtTest/QtTest>

// 原 DataProcessor/DataValidator/DataCleanerFormatter 管线已移除，
// 保留空测试确保构建目标不丢失
class TestDataProcessor : public QObject {
    Q_OBJECT
private slots:
    void test_placeholder() { QVERIFY(true); }
};

QTEST_MAIN(TestDataProcessor)
#include "test_dataprocessing.moc"
