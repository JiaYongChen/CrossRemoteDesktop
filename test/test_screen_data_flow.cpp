#include <QtTest/QtTest>
#include <QtCore/QObject>
#include <QtCore/QDebug>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtGui/QPainter>
#include <QtCore/QBuffer>

// 包含需要测试的类
#include "../src/common/core/logging/LoggingCategories.h"
#include "../src/common/core/network/Protocol.h"
#include "../src/common/types/CommonTypes.h"

/**
 * @brief 测试屏幕数据传输流程的核心功能
 *
 * 这个测试类验证屏幕数据的编码、解码功能：
 * 1. ScreenData结构的编码解码
 * 2. 图像数据的处理
 * 3. 数据完整性验证
 */
class TestScreenDataFlow : public QObject {
    Q_OBJECT

public:
    TestScreenDataFlow();
    ~TestScreenDataFlow();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 核心功能测试
    void test_screenDataEncoding();
    void test_screenDataDecoding();
    void test_imageProcessing();
    void test_dataIntegrity();

private:
    // 辅助方法
    void createTestImage();
    QByteArray imageToByteArray(const QImage& image, const char* format = "JPEG", int quality = 80);
    QImage byteArrayToImage(const QByteArray& data);

    // 测试数据
    QImage m_testImage;
    QImage m_smallTestImage;
};

TestScreenDataFlow::TestScreenDataFlow() {
}

TestScreenDataFlow::~TestScreenDataFlow() {
}

void TestScreenDataFlow::initTestCase() {
    qCDebug(lcTest) << "初始化屏幕数据流测试";

    // 创建测试图像
    createTestImage();
}

void TestScreenDataFlow::cleanupTestCase() {
    qCDebug(lcTest) << "清理屏幕数据流测试";
}

void TestScreenDataFlow::init() {
    // 每个测试前的初始化
}

void TestScreenDataFlow::cleanup() {
    // 每个测试后的清理
}

void TestScreenDataFlow::test_screenDataEncoding() {
    qCDebug(lcTest) << "测试ScreenData编码";

    // 创建ScreenData实例
    ScreenData screenData;
    screenData.x = 100;
    screenData.y = 200;
    screenData.width = 800;
    screenData.height = 600;
    screenData.imageType = 1; // JPEG

    // 准备图像数据
    QByteArray imageData = imageToByteArray(m_testImage);
    screenData.dataSize = imageData.size();
    screenData.imageData = imageData;

    // 测试编码
    QByteArray encoded = screenData.encode();

    // 验证编码结果
    QVERIFY(!encoded.isEmpty());
    QVERIFY(encoded.size() > 0);

    qCDebug(lcTest) << "编码数据大小:" << encoded.size();
    qCDebug(lcTest) << "ScreenData编码测试通过";
}

void TestScreenDataFlow::test_screenDataDecoding() {
    qCDebug(lcTest) << "测试ScreenData解码";

    // 创建原始ScreenData
    ScreenData originalData;
    originalData.x = 150;
    originalData.y = 250;
    originalData.width = 640;
    originalData.height = 480;
    originalData.imageType = 2; // PNG

    QByteArray imageData = imageToByteArray(m_smallTestImage, "PNG");
    originalData.dataSize = imageData.size();
    originalData.imageData = imageData;

    // 编码
    QByteArray encoded = originalData.encode();
    QVERIFY(!encoded.isEmpty());

    // 解码
    ScreenData decodedData;
    bool decodeSuccess = decodedData.decode(encoded);

    // 验证解码结果
    QVERIFY(decodeSuccess);
    QCOMPARE(decodedData.x, originalData.x);
    QCOMPARE(decodedData.y, originalData.y);
    QCOMPARE(decodedData.width, originalData.width);
    QCOMPARE(decodedData.height, originalData.height);
    QCOMPARE(decodedData.imageType, originalData.imageType);
    QCOMPARE(decodedData.dataSize, originalData.dataSize);
    QCOMPARE(decodedData.imageData, originalData.imageData);

    qCDebug(lcTest) << "ScreenData解码测试通过";
}

void TestScreenDataFlow::test_imageProcessing() {
    qCDebug(lcTest) << "测试图像处理";

    // 测试图像转换
    QByteArray jpegData = imageToByteArray(m_testImage, "JPEG", 85);
    QByteArray pngData = imageToByteArray(m_testImage, "PNG");

    QVERIFY(!jpegData.isEmpty());
    QVERIFY(!pngData.isEmpty());

    // 测试图像恢复
    QImage jpegImage = byteArrayToImage(jpegData);
    QImage pngImage = byteArrayToImage(pngData);

    QVERIFY(!jpegImage.isNull());
    QVERIFY(!pngImage.isNull());
    QCOMPARE(jpegImage.size(), m_testImage.size());
    QCOMPARE(pngImage.size(), m_testImage.size());

    qCDebug(lcTest) << "JPEG数据大小:" << jpegData.size();
    qCDebug(lcTest) << "PNG数据大小:" << pngData.size();
    qCDebug(lcTest) << "图像处理测试通过";
}

void TestScreenDataFlow::test_dataIntegrity() {
    qCDebug(lcTest) << "测试数据完整性";

    // 创建多个ScreenData进行完整性测试
    QList<ScreenData> testDataList;

    for ( int i = 0; i < 5; ++i ) {
        ScreenData data;
        data.x = i * 100;
        data.y = i * 50;
        data.width = 800 + i * 10;
        data.height = 600 + i * 10;
        data.imageType = 1; // JPEG

        QByteArray imageData = imageToByteArray(m_testImage);
        data.dataSize = imageData.size();
        data.imageData = imageData;

        testDataList.append(data);
    }

    // 测试每个数据的编码解码完整性
    for ( int i = 0; i < testDataList.size(); ++i ) {
        const ScreenData& original = testDataList[i];

        // 编码
        QByteArray encoded = original.encode();
        QVERIFY(!encoded.isEmpty());

        // 解码
        ScreenData decoded;
        bool success = decoded.decode(encoded);
        QVERIFY(success);

        // 验证完整性
        QCOMPARE(decoded.x, original.x);
        QCOMPARE(decoded.y, original.y);
        QCOMPARE(decoded.width, original.width);
        QCOMPARE(decoded.height, original.height);
        QCOMPARE(decoded.imageType, original.imageType);
        QCOMPARE(decoded.dataSize, original.dataSize);
        QCOMPARE(decoded.imageData.size(), original.imageData.size());

        // 验证图像数据
        QImage originalImage = byteArrayToImage(original.imageData);
        QImage decodedImage = byteArrayToImage(decoded.imageData);
        QVERIFY(!originalImage.isNull());
        QVERIFY(!decodedImage.isNull());
        QCOMPARE(decodedImage.size(), originalImage.size());
    }

    qCDebug(lcTest) << "数据完整性测试通过，测试了" << testDataList.size() << "个数据包";
}

void TestScreenDataFlow::createTestImage() {
    // 创建标准测试图像
    m_testImage = QImage(800, 600, QImage::Format_RGB32);
    m_testImage.fill(Qt::white);

    QPainter painter(&m_testImage);
    painter.setRenderHint(QPainter::Antialiasing);

    // 绘制一些图形
    painter.setPen(QPen(Qt::black, 2));
    painter.setBrush(Qt::red);
    painter.drawRect(50, 50, 200, 150);

    painter.setBrush(Qt::green);
    painter.drawEllipse(300, 100, 150, 100);

    painter.setBrush(Qt::blue);
    painter.drawPolygon(QPolygon() << QPoint(500, 50) << QPoint(600, 150) << QPoint(450, 200));

    painter.setPen(QPen(Qt::magenta, 3));
    painter.drawLine(100, 300, 700, 400);

    painter.end();

    // 创建小测试图像
    m_smallTestImage = QImage(200, 150, QImage::Format_RGB32);
    m_smallTestImage.fill(Qt::lightGray);

    QPainter smallPainter(&m_smallTestImage);
    smallPainter.setBrush(Qt::yellow);
    smallPainter.drawRect(20, 20, 160, 110);
    smallPainter.setPen(QPen(Qt::darkBlue, 2));
    smallPainter.drawText(50, 80, "Test Image");
    smallPainter.end();

    qCDebug(lcTest) << "测试图像创建完成 - 主图像:" << m_testImage.size() << "小图像:" << m_smallTestImage.size();
}

QByteArray TestScreenDataFlow::imageToByteArray(const QImage& image, const char* format, int quality) {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);

    bool success = image.save(&buffer, format, quality);
    buffer.close();

    if ( !success ) {
        qCWarning(lcTest) << "图像保存失败，格式:" << format;
        return QByteArray();
    }

    return data;
}

QImage TestScreenDataFlow::byteArrayToImage(const QByteArray& data) {
    QImage image;
    bool success = image.loadFromData(data);

    if ( !success ) {
        qCWarning(lcTest) << "图像加载失败，数据大小:" << data.size();
    }

    return image;
}

QTEST_MAIN(TestScreenDataFlow)
#include "test_screen_data_flow.moc"