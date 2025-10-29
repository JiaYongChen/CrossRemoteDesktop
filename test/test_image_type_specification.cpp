#include <QtTest/QtTest>
#include <QtGui/QImage>
#include <QtCore/QBuffer>
#include "../src/common/core/network/Protocol.h"
#include "../src/common/core/compression/Compression.h"
#include "../src/client/TcpClient.h"
#include "../src/server/ServerManager.h"

class TestImageTypeSpecification : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testServerSpecifiesJpegType();
    void testServerSpecifiesPngType();
    void testClientHandlesUnknownType();
    void testImageTypeConsistency();
    void cleanupTestCase();

private:
    QImage createTestImage();
    ScreenData createScreenDataWithImageType(const QImage& image, Compression::ImageFormat format);
};

void TestImageTypeSpecification::initTestCase() {
    qDebug() << "开始图像类型指定测试";
}

QImage TestImageTypeSpecification::createTestImage() {
    QImage image(200, 150, QImage::Format_RGB32);
    image.fill(Qt::blue);

    // 添加一些图案使图像更有特征
    for ( int x = 0; x < image.width(); x += 20 ) {
        for ( int y = 0; y < image.height(); y += 20 ) {
            image.setPixel(x, y, qRgb(255, 0, 0)); // 红色点
        }
    }

    return image;
}

ScreenData TestImageTypeSpecification::createScreenDataWithImageType(const QImage& image, Compression::ImageFormat format) {
    ScreenData screenData;
    screenData.x = 0;
    screenData.y = 0;
    screenData.width = image.width();
    screenData.height = image.height();
    screenData.imageType = static_cast<quint8>(format);
    screenData.compressionType = 0;

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);

    const char* formatStr = "JPEG";
    if ( format == Compression::ImageFormat::PNG ) {
        formatStr = "PNG";
    } else if ( format == Compression::ImageFormat::WEBP ) {
        formatStr = "WEBP";
    }

    bool success = image.save(&buffer, formatStr, 85);
    if ( !success ) {
        qWarning() << "Failed to save image with format:" << formatStr;
        return screenData; // 返回空的screenData
    }

    screenData.imageData = buffer.data();
    screenData.dataSize = screenData.imageData.size();

    return screenData;
}

void TestImageTypeSpecification::testServerSpecifiesJpegType() {
    qDebug() << "测试服务端指定JPEG类型";

    QImage originalImage = createTestImage();
    ScreenData screenData = createScreenDataWithImageType(originalImage, Compression::ImageFormat::JPEG);

    // 验证服务端正确设置了图像类型
    QCOMPARE(screenData.imageType, static_cast<quint8>(Compression::ImageFormat::JPEG));

    // 验证图像数据格式
    Compression::ImageFormat detectedFormat = Compression::detectImageFormat(screenData.imageData);
    QCOMPARE(detectedFormat, Compression::ImageFormat::JPEG);

    // 模拟客户端解析
    QImage decodedImage;
    Compression::ImageFormat specifiedFormat = static_cast<Compression::ImageFormat>(screenData.imageType);

    if ( specifiedFormat == Compression::ImageFormat::JPEG ) {
        bool loaded = decodedImage.loadFromData(screenData.imageData, "JPEG");
        QVERIFY(loaded);
        QVERIFY(!decodedImage.isNull());
        QCOMPARE(decodedImage.size(), originalImage.size());
    }

    qDebug() << "JPEG类型指定测试通过，数据大小:" << screenData.imageData.size();
}

void TestImageTypeSpecification::testServerSpecifiesPngType() {
    qDebug() << "测试服务端指定PNG类型";

    QImage originalImage = createTestImage();
    ScreenData screenData = createScreenDataWithImageType(originalImage, Compression::ImageFormat::PNG);

    // 验证服务端正确设置了图像类型
    QCOMPARE(screenData.imageType, static_cast<quint8>(Compression::ImageFormat::PNG));

    // 验证图像数据格式
    Compression::ImageFormat detectedFormat = Compression::detectImageFormat(screenData.imageData);
    QCOMPARE(detectedFormat, Compression::ImageFormat::PNG);

    // 模拟客户端解析
    QImage decodedImage;
    Compression::ImageFormat specifiedFormat = static_cast<Compression::ImageFormat>(screenData.imageType);

    if ( specifiedFormat == Compression::ImageFormat::PNG ) {
        bool loaded = decodedImage.loadFromData(screenData.imageData, "PNG");
        QVERIFY(loaded);
        QVERIFY(!decodedImage.isNull());
        QCOMPARE(decodedImage.size(), originalImage.size());
    }

    qDebug() << "PNG类型指定测试通过，数据大小:" << screenData.imageData.size();
}

void TestImageTypeSpecification::testClientHandlesUnknownType() {
    qDebug() << "测试客户端处理未知类型";

    QImage originalImage = createTestImage();
    ScreenData screenData = createScreenDataWithImageType(originalImage, Compression::ImageFormat::JPEG);

    // 故意设置一个未知的图像类型
    screenData.imageType = 99; // 未知类型

    // 模拟客户端解析 - 应该回退到格式检测
    QImage decodedImage;
    Compression::ImageFormat specifiedFormat = static_cast<Compression::ImageFormat>(screenData.imageType);

    // 由于是未知类型，应该使用格式检测作为回退
    if ( specifiedFormat != Compression::ImageFormat::JPEG &&
        specifiedFormat != Compression::ImageFormat::PNG &&
        specifiedFormat != Compression::ImageFormat::WEBP &&
        specifiedFormat != Compression::ImageFormat::BMP ) {

        // 使用格式检测
        Compression::ImageFormat detectedFormat = Compression::detectImageFormat(screenData.imageData);
        QCOMPARE(detectedFormat, Compression::ImageFormat::JPEG); // 实际数据是JPEG

        bool loaded = decodedImage.loadFromData(screenData.imageData, "JPEG");
        QVERIFY(loaded);
        QVERIFY(!decodedImage.isNull());
    }

    qDebug() << "未知类型处理测试通过";
}

void TestImageTypeSpecification::testImageTypeConsistency() {
    qDebug() << "测试图像类型一致性";

    QImage originalImage = createTestImage();

    // 测试JPEG格式一致性
    ScreenData jpegData = createScreenDataWithImageType(originalImage, Compression::ImageFormat::JPEG);
    Compression::ImageFormat jpegDetected = Compression::detectImageFormat(jpegData.imageData);
    QCOMPARE(static_cast<quint8>(jpegDetected), jpegData.imageType);

    // 测试PNG格式一致性
    ScreenData pngData = createScreenDataWithImageType(originalImage, Compression::ImageFormat::PNG);
    Compression::ImageFormat pngDetected = Compression::detectImageFormat(pngData.imageData);
    QCOMPARE(static_cast<quint8>(pngDetected), pngData.imageType);

    qDebug() << "图像类型一致性测试通过";
    qDebug() << "JPEG - 指定类型:" << jpegData.imageType << "检测类型:" << static_cast<quint8>(jpegDetected);
    qDebug() << "PNG - 指定类型:" << pngData.imageType << "检测类型:" << static_cast<quint8>(pngDetected);
}

void TestImageTypeSpecification::cleanupTestCase() {
    qDebug() << "图像类型指定测试完成";
}

QTEST_MAIN(TestImageTypeSpecification)
#include "TestImageTypeSpecification.moc"