#include <QtTest/QtTest>
#include <QtCore/QObject>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtCore/QBuffer>
#include <QtCore/QDebug>
#include "../src/common/core/protocol.h"
#include "../src/common/core/logging_categories.h"

/**
 * @brief 测试屏幕数据传输的编码和解码功能
 * 
 * 该测试类验证ScreenData结构体的编码解码功能，
 * 以及图像数据的正确传输和转换。
 */
class TestScreenDataTransmission : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief 初始化测试用例
     */
    void initTestCase();
    
    /**
     * @brief 清理测试用例
     */
    void cleanupTestCase();
    
    /**
     * @brief 测试ScreenData结构体的编码和解码
     */
    void testScreenDataCodec();
    
    /**
     * @brief 测试图像数据的JPEG编码和解码
     */
    void testImageJpegEncoding();
    
    /**
     * @brief 测试图像数据的PNG编码和解码
     */
    void testImagePngEncoding();
    
    /**
     * @brief 测试空图像数据的处理
     */
    void testEmptyImageData();
    
    /**
     * @brief 测试损坏数据的处理
     */
    void testCorruptedData();
    
    /**
     * @brief 测试大尺寸图像的处理
     */
    void testLargeImageData();

private:
    /**
     * @brief 创建测试用的QImage
     * @param width 图像宽度
     * @param height 图像高度
     * @return 创建的测试图像
     */
    QImage createTestImage(int width, int height);
    
    /**
     * @brief 将QImage编码为指定格式的字节数组
     * @param image 要编码的图像
     * @param format 图像格式（如"JPEG"、"PNG"）
     * @param quality 图像质量（0-100，仅对JPEG有效）
     * @return 编码后的字节数组
     */
    QByteArray encodeImage(const QImage &image, const char *format, int quality = 95);
};

void TestScreenDataTransmission::initTestCase()
{
    qDebug() << "开始屏幕数据传输测试";
}

void TestScreenDataTransmission::cleanupTestCase()
{
    qDebug() << "屏幕数据传输测试完成";
}

void TestScreenDataTransmission::testScreenDataCodec()
{
    // 创建测试图像
    QImage testImage = createTestImage(800, 600);
    QVERIFY(!testImage.isNull());
    
    // 将图像编码为JPEG格式
    QByteArray imageData = encodeImage(testImage, "JPEG", 95);
    QVERIFY(!imageData.isEmpty());
    
    // 创建ScreenData结构体
    ScreenData screenData;
    screenData.x = 0;
    screenData.y = 0;
    screenData.width = static_cast<quint16>(testImage.width());
    screenData.height = static_cast<quint16>(testImage.height());
    screenData.imageType = 1; // JPEG
    screenData.compressionType = 0; // 无压缩
    screenData.dataSize = static_cast<quint32>(imageData.size());
    screenData.imageData = imageData;
    
    // 编码ScreenData
    QByteArray encodedData = screenData.encode();
    QVERIFY(!encodedData.isEmpty());
    
    // 解码ScreenData
    ScreenData decodedScreenData;
    bool decodeSuccess = decodedScreenData.decode(encodedData);
    QVERIFY(decodeSuccess);
    
    // 验证解码后的数据
    QCOMPARE(decodedScreenData.x, screenData.x);
    QCOMPARE(decodedScreenData.y, screenData.y);
    QCOMPARE(decodedScreenData.width, screenData.width);
    QCOMPARE(decodedScreenData.height, screenData.height);
    QCOMPARE(decodedScreenData.imageType, screenData.imageType);
    QCOMPARE(decodedScreenData.compressionType, screenData.compressionType);
    QCOMPARE(decodedScreenData.dataSize, screenData.dataSize);
    QCOMPARE(decodedScreenData.imageData, screenData.imageData);
    
    // 验证图像数据可以正确加载
    QImage decodedImage;
    bool imageLoadSuccess = decodedImage.loadFromData(decodedScreenData.imageData, "JPEG");
    QVERIFY(imageLoadSuccess);
    QVERIFY(!decodedImage.isNull());
    QCOMPARE(decodedImage.size(), testImage.size());
    
    qDebug() << "ScreenData编码解码测试通过";
}

void TestScreenDataTransmission::testImageJpegEncoding()
{
    // 创建测试图像
    QImage testImage = createTestImage(640, 480);
    QVERIFY(!testImage.isNull());
    
    // 测试不同质量的JPEG编码
    QList<int> qualities = {50, 75, 95};
    
    for (int quality : qualities) {
        QByteArray jpegData = encodeImage(testImage, "JPEG", quality);
        QVERIFY(!jpegData.isEmpty());
        
        // 验证可以正确解码
        QImage decodedImage;
        bool loadSuccess = decodedImage.loadFromData(jpegData, "JPEG");
        QVERIFY(loadSuccess);
        QVERIFY(!decodedImage.isNull());
        QCOMPARE(decodedImage.size(), testImage.size());
        
        qDebug() << "JPEG质量" << quality << "编码测试通过，数据大小:" << jpegData.size();
    }
}

void TestScreenDataTransmission::testImagePngEncoding()
{
    // 创建测试图像
    QImage testImage = createTestImage(320, 240);
    QVERIFY(!testImage.isNull());
    
    // PNG编码
    QByteArray pngData = encodeImage(testImage, "PNG");
    QVERIFY(!pngData.isEmpty());
    
    // 验证可以正确解码
    QImage decodedImage;
    bool loadSuccess = decodedImage.loadFromData(pngData, "PNG");
    QVERIFY(loadSuccess);
    QVERIFY(!decodedImage.isNull());
    QCOMPARE(decodedImage.size(), testImage.size());
    
    qDebug() << "PNG编码测试通过，数据大小:" << pngData.size();
}

void TestScreenDataTransmission::testEmptyImageData()
{
    // 测试空图像数据的处理
    ScreenData screenData;
    screenData.x = 0;
    screenData.y = 0;
    screenData.width = 0;
    screenData.height = 0;
    screenData.imageType = 1;
    screenData.compressionType = 0;
    screenData.dataSize = 0;
    screenData.imageData = QByteArray();
    
    // 编码空数据
    QByteArray encodedData = screenData.encode();
    QVERIFY(!encodedData.isEmpty()); // 头部数据仍然存在
    
    // 解码空数据 - 应该失败，因为宽度和高度为0是无效的
    ScreenData decodedScreenData;
    bool decodeSuccess = decodedScreenData.decode(encodedData);
    QVERIFY(!decodeSuccess); // 期望解码失败
    
    qDebug() << "空图像数据处理测试通过 - 正确拒绝了无效的空图像数据";
}

void TestScreenDataTransmission::testCorruptedData()
{
    // 测试损坏数据的处理
    QByteArray corruptedData("这是一些损坏的数据");
    
    ScreenData screenData;
    bool decodeSuccess = screenData.decode(corruptedData);
    QVERIFY(!decodeSuccess); // 应该解码失败
    
    // 测试不完整的数据
    QByteArray incompleteData;
    incompleteData.resize(5); // 小于最小头部大小
    
    ScreenData screenData2;
    bool decodeSuccess2 = screenData2.decode(incompleteData);
    QVERIFY(!decodeSuccess2); // 应该解码失败
    
    qDebug() << "损坏数据处理测试通过";
}

void TestScreenDataTransmission::testLargeImageData()
{
    // 测试大尺寸图像的处理
    QImage largeImage = createTestImage(1920, 1080);
    QVERIFY(!largeImage.isNull());
    
    QByteArray imageData = encodeImage(largeImage, "JPEG", 85);
    QVERIFY(!imageData.isEmpty());
    
    // 创建ScreenData
    ScreenData screenData;
    screenData.x = 0;
    screenData.y = 0;
    screenData.width = static_cast<quint16>(largeImage.width());
    screenData.height = static_cast<quint16>(largeImage.height());
    screenData.imageType = 1;
    screenData.compressionType = 0;
    screenData.dataSize = static_cast<quint32>(imageData.size());
    screenData.imageData = imageData;
    
    // 编码和解码
    QByteArray encodedData = screenData.encode();
    QVERIFY(!encodedData.isEmpty());
    
    ScreenData decodedScreenData;
    bool decodeSuccess = decodedScreenData.decode(encodedData);
    QVERIFY(decodeSuccess);
    
    // 验证图像数据
    QImage decodedImage;
    bool imageLoadSuccess = decodedImage.loadFromData(decodedScreenData.imageData, "JPEG");
    QVERIFY(imageLoadSuccess);
    QCOMPARE(decodedImage.size(), largeImage.size());
    
    qDebug() << "大尺寸图像处理测试通过，图像尺寸:" << largeImage.size() 
             << "数据大小:" << imageData.size();
}

QImage TestScreenDataTransmission::createTestImage(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    
    // 创建渐变色彩图像
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int red = (x * 255) / width;
            int green = (y * 255) / height;
            int blue = ((x + y) * 255) / (width + height);
            image.setPixel(x, y, qRgb(red, green, blue));
        }
    }
    
    return image;
}

QByteArray TestScreenDataTransmission::encodeImage(const QImage &image, const char *format, int quality)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    
    bool success = image.save(&buffer, format, quality);
    if (!success) {
        qWarning() << "Failed to encode image as" << format;
        return QByteArray();
    }
    
    return data;
}

QTEST_MAIN(TestScreenDataTransmission)
#include "test_screen_data_transmission.moc"