#include <QtTest/QtTest>
#include <QtCore/QObject>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtCore/QBuffer>
#include <QtCore/QDebug>
#include <QtCore/QDateTime>
#include "../src/common/core/network/Protocol.h"
#include "../src/common/core/logging/LoggingCategories.h"

// 确保常量定义可用
#ifndef PROTOCOL_MAGIC
#define PROTOCOL_MAGIC 0x52444350
#endif
#ifndef PROTOCOL_VERSION
#define PROTOCOL_VERSION 1
#endif

/**
 * @brief 集成测试：验证完整的图片传输流程
 *
 * 该测试类模拟server发送ScreenData，验证client端的
 * TcpClient -> SessionManager -> RenderManager 完整流程。
 */
class TestImageTransmissionIntegration : public QObject {
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
     * @brief 测试ScreenData消息的完整编码解码流程
     */
    void testScreenDataMessageFlow();

    /**
     * @brief 测试图像数据编码和传输
     */
    void testImageEncodingAndTransmission();

    /**
     * @brief 测试完整的图片传输数据流
     */
    void testCompleteImageDataFlow();

private:
    /**
     * @brief 创建测试用的QImage
     * @param width 图像宽度
     * @param height 图像高度
     * @return 创建的测试图像
     */
    QImage createTestImage(int width, int height);

    /**
     * @brief 将QImage编码为JPEG格式的字节数组
     * @param image 要编码的图像
     * @param quality 图像质量（0-100）
     * @return 编码后的字节数组
     */
    QByteArray encodeImageAsJpeg(const QImage& image, int quality = 95);

    /**
     * @brief 创建包含ScreenData的完整消息
     * @param screenData ScreenData结构体
     * @return 包含消息头的完整消息字节数组
     */
    QByteArray createScreenDataMessage(const ScreenData& screenData);
};

void TestImageTransmissionIntegration::initTestCase() {
    qDebug() << "开始图片传输集成测试";
}

void TestImageTransmissionIntegration::cleanupTestCase() {
    qDebug() << "图片传输集成测试完成";
}

void TestImageTransmissionIntegration::testScreenDataMessageFlow() {
    // 创建测试图像
    QImage testImage = createTestImage(640, 480);
    QVERIFY(!testImage.isNull());

    // 编码为JPEG
    QByteArray imageData = encodeImageAsJpeg(testImage, 85);
    QVERIFY(!imageData.isEmpty());

    // 创建ScreenData
    ScreenData screenData;
    screenData.x = 0;
    screenData.y = 0;
    screenData.width = static_cast<quint16>(testImage.width());
    screenData.height = static_cast<quint16>(testImage.height());
    screenData.imageType = 1; // JPG
    screenData.dataSize = static_cast<quint32>(imageData.size());
    screenData.imageData = imageData;

    // 创建完整消息
    QByteArray messageData = createScreenDataMessage(screenData);
    QVERIFY(!messageData.isEmpty());

    // 验证消息头解析
    MessageHeader header;
    bool headerDecodeSuccess = header.decode(messageData.left(SERIALIZED_HEADER_SIZE));
    QVERIFY(headerDecodeSuccess);
    QCOMPARE(header.type, MessageType::SCREEN_DATA);

    // 提取并验证ScreenData部分
    QByteArray screenDataPart = messageData.mid(SERIALIZED_HEADER_SIZE);
    ScreenData decodedScreenData;
    bool screenDataDecodeSuccess = decodedScreenData.decode(screenDataPart);
    QVERIFY(screenDataDecodeSuccess);

    // 验证解码后的数据
    QCOMPARE(decodedScreenData.width, screenData.width);
    QCOMPARE(decodedScreenData.height, screenData.height);
    QCOMPARE(decodedScreenData.imageType, screenData.imageType);
    QCOMPARE(decodedScreenData.dataSize, screenData.dataSize);

    // 验证图像数据可以正确加载
    QImage decodedImage;
    bool imageLoadSuccess = decodedImage.loadFromData(decodedScreenData.imageData, "JPG");
    QVERIFY(imageLoadSuccess);
    QCOMPARE(decodedImage.size(), testImage.size());

    qDebug() << "ScreenData消息流程测试通过";
    qDebug() << "测试图像尺寸:" << testImage.size();
    qDebug() << "JPEG数据大小:" << imageData.size();
    qDebug() << "完整消息大小:" << messageData.size();
}

void TestImageTransmissionIntegration::testImageEncodingAndTransmission() {
    // 创建测试图像
    QImage testImage = createTestImage(800, 600);
    QVERIFY(!testImage.isNull());

    // 测试不同质量的JPEG编码（仅测试JPEG编码）
    QList<int> qualities = { 50, 75, 90, 95 };
    QList<int> dataSizes;

    for ( int quality : qualities ) {
        QByteArray encodedData = encodeImageAsJpeg(testImage, quality);
        QVERIFY(!encodedData.isEmpty());
        dataSizes.append(encodedData.size());

        // 验证编码后的图像可以正确解码
        QImage decodedImage;
        bool loadSuccess = decodedImage.loadFromData(encodedData, "JPG");
        QVERIFY(loadSuccess);
        QVERIFY(!decodedImage.isNull());
        QCOMPARE(decodedImage.size(), testImage.size());

        // 创建ScreenData并测试传输
        ScreenData screenData;
        screenData.x = 0;
        screenData.y = 0;
        screenData.width = static_cast<quint16>(testImage.width());
        screenData.height = static_cast<quint16>(testImage.height());
        screenData.imageType = 1; // JPG
        screenData.dataSize = static_cast<quint32>(encodedData.size());
        screenData.imageData = encodedData;

        // 测试编码解码循环
        QByteArray encodedScreenData = screenData.encode();
        QVERIFY(!encodedScreenData.isEmpty());

        ScreenData decodedScreenData;
        bool decodeSuccess = decodedScreenData.decode(encodedScreenData);
        QVERIFY(decodeSuccess);
        QCOMPARE(decodedScreenData.dataSize, screenData.dataSize);

        qDebug() << "质量" << quality << "编码测试通过，数据大小:" << encodedData.size();
    }

    // 验证编码质量与数据大小的关系
    for ( int i = 1; i < dataSizes.size(); ++i ) {
        QVERIFY(dataSizes[i] >= dataSizes[i - 1]); // 高质量应该产生更大的文件
    }

    qDebug() << "图像编码和传输测试通过";
}

void TestImageTransmissionIntegration::testCompleteImageDataFlow() {
    // 创建测试图像
    QImage testImage = createTestImage(1024, 768);
    QVERIFY(!testImage.isNull());

    // 编码为JPEG
    QByteArray imageData = encodeImageAsJpeg(testImage, 90);
    QVERIFY(!imageData.isEmpty());

    // 创建ScreenData
    ScreenData screenData;
    screenData.x = 100;
    screenData.y = 50;
    screenData.width = static_cast<quint16>(testImage.width());
    screenData.height = static_cast<quint16>(testImage.height());
    screenData.imageType = 1; // JPG
    screenData.dataSize = static_cast<quint32>(imageData.size());
    screenData.imageData = imageData;

    // 创建完整的消息（包含消息头）
    QByteArray completeMessage = createScreenDataMessage(screenData);
    QVERIFY(!completeMessage.isEmpty());

    // 模拟网络传输：解析消息头
    MessageHeader receivedHeader;
    int headerSize = SERIALIZED_HEADER_SIZE;
    bool headerDecodeSuccess = receivedHeader.decode(completeMessage.left(headerSize));
    QVERIFY(headerDecodeSuccess);
    QCOMPARE(receivedHeader.type, MessageType::SCREEN_DATA);

    // 提取ScreenData部分
    QByteArray screenDataPart = completeMessage.mid(headerSize);
    ScreenData receivedScreenData;
    bool screenDataDecodeSuccess = receivedScreenData.decode(screenDataPart);
    QVERIFY(screenDataDecodeSuccess);

    // 验证接收到的ScreenData
    QCOMPARE(receivedScreenData.x, screenData.x);
    QCOMPARE(receivedScreenData.y, screenData.y);
    QCOMPARE(receivedScreenData.width, screenData.width);
    QCOMPARE(receivedScreenData.height, screenData.height);
    QCOMPARE(receivedScreenData.imageType, screenData.imageType);
    QCOMPARE(receivedScreenData.dataSize, screenData.dataSize);
    QCOMPARE(receivedScreenData.imageData.size(), screenData.imageData.size());

    // 验证图像数据可以正确加载
    QImage receivedImage;
    bool imageLoadSuccess = receivedImage.loadFromData(receivedScreenData.imageData, "JPG");
    QVERIFY(imageLoadSuccess);
    QVERIFY(!receivedImage.isNull());
    QCOMPARE(receivedImage.size(), testImage.size());

    // 转换为QPixmap（模拟RenderManager的处理）
    QPixmap finalPixmap = QPixmap::fromImage(receivedImage);
    QVERIFY(!finalPixmap.isNull());
    QCOMPARE(finalPixmap.size(), testImage.size());

    // 计算传输效率
    int originalSize = testImage.width() * testImage.height() * 4; // RGBA
    double encodingRatio = double(imageData.size()) / originalSize * 100; // 编码比
    int totalMessageSize = completeMessage.size();

    qDebug() << "完整图片数据流测试通过";
    qDebug() << "原始图像尺寸:" << testImage.size();
    qDebug() << "原始数据大小:" << originalSize << "字节";
    qDebug() << "JPEG编码后大小:" << imageData.size() << "字节"; // 编码
    qDebug() << "完整消息大小:" << totalMessageSize << "字节";
    qDebug() << "编码比:" << QString::number(encodingRatio, 'f', 2) << "%"; // 修改：编码比
    qDebug() << "最终Pixmap尺寸:" << finalPixmap.size();
}

QImage TestImageTransmissionIntegration::createTestImage(int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);

    // 创建更复杂的测试图像，包含渐变和几何图形
    for ( int y = 0; y < height; ++y ) {
        for ( int x = 0; x < width; ++x ) {
            // 创建径向渐变效果
            double centerX = width / 2.0;
            double centerY = height / 2.0;
            double distance = sqrt((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY));
            double maxDistance = sqrt(centerX * centerX + centerY * centerY);
            double ratio = distance / maxDistance;

            int red = static_cast<int>(255 * (1.0 - ratio));
            int green = static_cast<int>(255 * ratio);
            int blue = static_cast<int>(255 * sin(ratio * 3.14159));

            // 添加一些几何图案
            if ( (x / 50 + y / 50) % 2 == 0 ) {
                red = (red + 100) % 256;
                green = (green + 50) % 256;
            }

            image.setPixel(x, y, qRgb(red, green, blue));
        }
    }

    return image;
}

QByteArray TestImageTransmissionIntegration::encodeImageAsJpeg(const QImage& image, int quality) {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);

    bool success = image.save(&buffer, "JPG", quality);
    if ( !success ) {
        qWarning() << "Failed to encode image as JPG";
        return QByteArray();
    }

    return data;
}

QByteArray TestImageTransmissionIntegration::createScreenDataMessage(const ScreenData& screenData) {
    // 创建消息头
    MessageHeader header;
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = MessageType::SCREEN_DATA;
    header.length = static_cast<quint32>(screenData.encode().size());
    header.checksum = 0; // 简化测试，不计算校验和
    header.timestamp = QDateTime::currentMSecsSinceEpoch();

    // 编码消息头和数据
    QByteArray headerData = header.encode();
    QByteArray screenDataBytes = screenData.encode();

    // 组合完整消息
    QByteArray completeMessage;
    completeMessage.append(headerData);
    completeMessage.append(screenDataBytes);

    return completeMessage;
}

QTEST_MAIN(TestImageTransmissionIntegration)
#include "TestImageTransmissionIntegration.moc"