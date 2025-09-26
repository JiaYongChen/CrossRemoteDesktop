#include <QtTest/QtTest>
#include "../src/server/dataprocessing/DataProcessing.h"

// 测试类命名：TestDataProcessor，遵循要求：测试类名与被测试类名相同，前缀为 Test
class TestDataProcessor : public QObject {
    Q_OBJECT
private slots:
    // 测试函数命名：test_ 加上被测函数名
    void test_processAndStore_validImage();
    void test_processAndStore_invalidMime();
    void test_processAndStore_emptyPayload();
    void test_retrieve_success();
    void test_retrieve_notFound();
    void test_cleaner_formatter_image_to_argb32();
};

void TestDataProcessor::test_processAndStore_validImage() {
    DataProcessor processor;

    // 准备一个小的PNG图像字节（通过QImage生成）
    QImage img(2, 2, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QByteArray pngBytes;
    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(img.save(&buffer, "PNG"));

    QString id, err;
    const bool ok = processor.processAndStore(pngBytes, "image/png", id, err);
    QVERIFY2(ok, qPrintable(QString("期望验证与清洗成功，但失败：%1").arg(err)));
    QVERIFY(!id.isEmpty());
}

void TestDataProcessor::test_processAndStore_invalidMime() {
    DataProcessor processor;

    // 伪造字节但提供错误MIME类型
    QByteArray raw("abcdefg");
    QString id, err;
    const bool ok = processor.processAndStore(raw, "", id, err);
    QVERIFY(!ok);
}

void TestDataProcessor::test_processAndStore_emptyPayload() {
    DataProcessor processor;

    // 空payload
    QByteArray raw;
    QString id, err;
    const bool ok = processor.processAndStore(raw, "application/octet-stream", id, err);
    QVERIFY(!ok);
}

void TestDataProcessor::test_retrieve_success() {
    DataProcessor processor;

    // 存一条合法数据
    QImage img(1, 1, QImage::Format_ARGB32);
    img.fill(Qt::blue);
    QByteArray pngBytes;
    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(img.save(&buffer, "PNG"));

    QString id, err;
    QVERIFY(processor.processAndStore(pngBytes, "image/png", id, err));

    DataRecord out;
    QString getErr;
    QVERIFY(processor.retrieve(id, out, getErr));
    QVERIFY(out.checksum != 0);
    QVERIFY(!out.payload.isEmpty());
}

void TestDataProcessor::test_retrieve_notFound() {
    DataProcessor processor;
    DataRecord out;
    QString getErr;
    const bool ok = processor.retrieve("non-exists-id", out, getErr);
    QVERIFY(!ok);
}

void TestDataProcessor::test_cleaner_formatter_image_to_argb32() {
    DataProcessor processor;

    // 生成PNG并处理存储
    QImage img(2, 2, QImage::Format_ARGB32);
    img.fill(Qt::green);
    QByteArray pngBytes;
    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(img.save(&buffer, "PNG"));

    QString id, err;
    QVERIFY(processor.processAndStore(pngBytes, "image/png", id, err));

    // 取出记录，检查MIME类型是否被转换为 application/x-raw-argb32
    DataRecord rec;
    QString getErr;
    QVERIFY(processor.retrieve(id, rec, getErr));
    QCOMPARE(rec.mimeType, QString("application/x-raw-argb32"));
    QVERIFY(rec.size.width() > 0 && rec.size.height() > 0);
}

QTEST_MAIN(TestDataProcessor)
#include "test_dataprocessing.moc"