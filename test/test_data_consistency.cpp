#include <QtTest/QtTest>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QCryptographicHash>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtCore/QBuffer>
#include <QtCore/QDataStream>
#include <QtCore/QRandomGenerator>
#include "../src/common/data/DataRecord.h"
#include "../src/server/dataprocessing/DataProcessing.h"

/**
 * @brief 数据一致性验证测试类
 *
 * 测试server和client之间的数据传输一致性，包括：
 * - 图像数据完整性验证
 * - 校验和验证
 * - 网络传输过程中的数据保护
 */
class TestDataConsistency : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 数据一致性测试
    void test_imageDataConsistency();
    void test_checksumVerification();
    void test_dataProcessingConsistency();
    void test_networkTransmissionIntegrity();
    void test_largeDataConsistency();
    void test_multipleFrameConsistency();

private:
    // 辅助方法
    QImage createTestImage(int width, int height, const QColor& color);
    QByteArray calculateChecksum(const QByteArray& data);
    bool compareImages(const QImage& img1, const QImage& img2);
    void waitForSignal(QObject* sender, const char* signal, int timeout = 5000);

private:
    DataValidator* m_dataValidator;
    DataCleanerFormatter* m_dataCleanerFormatter;
    QEventLoop* m_eventLoop;

    // 测试数据
    QList<QImage> m_testImages;
    QList<QByteArray> m_receivedData;
    QList<QString> m_checksums;
};

void TestDataConsistency::initTestCase() {
    qDebug() << "开始数据一致性验证测试";

    // 创建测试图像
    m_testImages.append(createTestImage(640, 480, Qt::red));
    m_testImages.append(createTestImage(800, 600, Qt::green));
    m_testImages.append(createTestImage(1024, 768, Qt::blue));
    m_testImages.append(createTestImage(320, 240, Qt::yellow));

    qDebug() << "创建了" << m_testImages.size() << "个测试图像";
}

void TestDataConsistency::cleanupTestCase() {
    qDebug() << "数据一致性验证测试完成";
}

void TestDataConsistency::init() {
    m_dataValidator = new DataValidator();
    m_dataCleanerFormatter = new DataCleanerFormatter();
    m_eventLoop = new QEventLoop();

    m_receivedData.clear();
    m_checksums.clear();

    QVERIFY(m_dataValidator != nullptr);
    QVERIFY(m_dataCleanerFormatter != nullptr);
}

void TestDataConsistency::cleanup() {
    if ( m_dataValidator ) {
        delete m_dataValidator;
        m_dataValidator = nullptr;
    }

    if ( m_dataCleanerFormatter ) {
        delete m_dataCleanerFormatter;
        m_dataCleanerFormatter = nullptr;
    }

    if ( m_eventLoop ) {
        delete m_eventLoop;
        m_eventLoop = nullptr;
    }
}

void TestDataConsistency::test_imageDataConsistency() {
    qDebug() << "测试图像数据一致性";

    // 测试每个图像的数据一致性
    for ( int i = 0; i < m_testImages.size(); ++i ) {
        const QImage& originalImage = m_testImages[i];

        // 将图像转换为字节数组
        QByteArray originalData;
        QBuffer buffer(&originalData);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(originalImage.save(&buffer, "PNG"));

        // 计算原始数据校验和
        QByteArray originalChecksum = calculateChecksum(originalData);

        // 模拟从字节数组重建图像
        QImage reconstructedImage;
        QVERIFY(reconstructedImage.loadFromData(originalData, "PNG"));

        // 验证图像一致性
        QVERIFY2(compareImages(originalImage, reconstructedImage),
            qPrintable(QString("图像 %1 数据不一致").arg(i)));

        // 验证重建后的数据校验和
        QByteArray reconstructedData;
        QBuffer reconstructedBuffer(&reconstructedData);
        reconstructedBuffer.open(QIODevice::WriteOnly);
        QVERIFY(reconstructedImage.save(&reconstructedBuffer, "PNG"));

        QByteArray reconstructedChecksum = calculateChecksum(reconstructedData);
        QCOMPARE(originalChecksum, reconstructedChecksum);

        qDebug() << QString("图像 %1 (%2x%3) 数据一致性验证通过")
            .arg(i).arg(originalImage.width()).arg(originalImage.height());
    }
}

void TestDataConsistency::test_checksumVerification() {
    qDebug() << "测试校验和验证";

    // 创建测试数据
    QByteArray testData = "这是用于校验和测试的数据";
    QByteArray checksum1 = calculateChecksum(testData);
    QByteArray checksum2 = calculateChecksum(testData);

    // 验证相同数据的校验和一致
    QCOMPARE(checksum1, checksum2);

    // 修改数据并验证校验和不同
    QByteArray modifiedData = testData + "修改";
    QByteArray modifiedChecksum = calculateChecksum(modifiedData);
    QVERIFY(checksum1 != modifiedChecksum);

    // 测试图像数据的校验和
    for ( const QImage& image : m_testImages ) {
        QByteArray imageData;
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");

        QByteArray checksum1 = calculateChecksum(imageData);
        QByteArray checksum2 = calculateChecksum(imageData);

        QCOMPARE(checksum1, checksum2);
        m_checksums.append(QString::fromUtf8(checksum1.toHex()));
    }

    qDebug() << "校验和验证通过，生成了" << m_checksums.size() << "个校验和";
}

void TestDataConsistency::test_dataProcessingConsistency() {
    qDebug() << "测试数据处理组件一致性";

    for ( int i = 0; i < m_testImages.size(); ++i ) {
        const QImage& originalImage = m_testImages[i];

        // 将图像转换为字节数组
        QByteArray originalData;
        QBuffer buffer(&originalData);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(originalImage.save(&buffer, "PNG"));

        // 使用DataValidator验证数据
        DataRecord validationRecord;
        bool validationResult = m_dataValidator->validate(originalData, "image/png", validationRecord);
        QVERIFY2(validationResult, "数据验证失败");

        // 验证DataRecord的完整性
        QVERIFY(!validationRecord.id.isEmpty());
        QCOMPARE(validationRecord.mimeType, QString("image/png"));
        QCOMPARE(validationRecord.payload, originalData);
        QVERIFY(validationRecord.checksum != 0);

        // 使用DataCleanerFormatter处理数据
        DataRecord cleanedRecord;
        QString cleaningError;
        bool cleaningResult = m_dataCleanerFormatter->cleanAndFormat(validationRecord, cleanedRecord, cleaningError);
        QVERIFY2(cleaningResult, qPrintable(QString("数据清洗失败: %1").arg(cleaningError)));

        // 验证清洗后的数据
        QVERIFY(!cleanedRecord.id.isEmpty());
        QCOMPARE(cleanedRecord.mimeType, QString("application/x-raw-argb32"));
        QVERIFY(!cleanedRecord.payload.isEmpty());
        QCOMPARE(cleanedRecord.size, originalImage.size());
        QVERIFY(cleanedRecord.checksum != 0);

        // 验证清洗后的数据可以重建图像
        QImage reconstructedImage(reinterpret_cast<const uchar*>(cleanedRecord.payload.constData()),
            cleanedRecord.size.width(), cleanedRecord.size.height(),
            QImage::Format_ARGB32);

        QVERIFY(!reconstructedImage.isNull());
        QCOMPARE(reconstructedImage.size(), originalImage.size());

        // 验证重建图像的基本属性
        QCOMPARE(reconstructedImage.width(), originalImage.width());
        QCOMPARE(reconstructedImage.height(), originalImage.height());

        qDebug() << QString("图像 %1 数据处理一致性验证通过").arg(i);
    }

    qDebug() << "数据处理组件一致性验证完成";
}

void TestDataConsistency::test_networkTransmissionIntegrity() {
    qDebug() << "测试网络传输完整性";

    // 创建测试数据包
    struct TestPacket {
        quint32 id;
        quint32 size;
        QByteArray data;
        QByteArray checksum;
    };

    QList<TestPacket> testPackets;

    // 创建不同大小的测试数据包
    QList<int> packetSizes = { 100, 1024, 4096, 16384, 65536 };

    for ( int i = 0; i < packetSizes.size(); ++i ) {
        TestPacket packet;
        packet.id = i;
        packet.size = packetSizes[i];

        // 生成随机数据
        packet.data.resize(packet.size);
        for ( quint32 j = 0; j < packet.size; ++j ) {
            packet.data[j] = static_cast<char>(QRandomGenerator::global()->bounded(256));
        }

        packet.checksum = calculateChecksum(packet.data);
        testPackets.append(packet);
    }

    // 模拟网络传输过程中的数据验证
    for ( const TestPacket& originalPacket : testPackets ) {
        // 模拟序列化
        QByteArray serializedData;
        QDataStream stream(&serializedData, QIODevice::WriteOnly);
        stream << originalPacket.id << originalPacket.size << originalPacket.data << originalPacket.checksum;

        // 模拟反序列化
        TestPacket receivedPacket;
        QDataStream receiveStream(&serializedData, QIODevice::ReadOnly);
        receiveStream >> receivedPacket.id >> receivedPacket.size >> receivedPacket.data >> receivedPacket.checksum;

        // 验证数据完整性
        QCOMPARE(receivedPacket.id, originalPacket.id);
        QCOMPARE(receivedPacket.size, originalPacket.size);
        QCOMPARE(receivedPacket.data, originalPacket.data);
        QCOMPARE(receivedPacket.checksum, originalPacket.checksum);

        // 验证接收数据的校验和
        QByteArray receivedChecksum = calculateChecksum(receivedPacket.data);
        QCOMPARE(receivedChecksum, receivedPacket.checksum);
    }

    qDebug() << "网络传输完整性验证通过，测试了" << testPackets.size() << "个数据包";
}

void TestDataConsistency::test_largeDataConsistency() {
    qDebug() << "测试大数据一致性";

    // 创建大图像（模拟高分辨率屏幕）
    QImage largeImage = createTestImage(1920, 1080, Qt::cyan);

    // 转换为字节数组
    QByteArray originalData;
    QBuffer buffer(&originalData);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(largeImage.save(&buffer, "PNG"));

    qDebug() << "大图像数据大小:" << originalData.size() << "字节";

    // 计算原始校验和
    QByteArray originalChecksum = calculateChecksum(originalData);

    // 模拟分块传输
    const int chunkSize = 8192; // 8KB 块
    QList<QByteArray> chunks;

    for ( int i = 0; i < originalData.size(); i += chunkSize ) {
        int currentChunkSize = qMin(chunkSize, originalData.size() - i);
        QByteArray chunk = originalData.mid(i, currentChunkSize);
        chunks.append(chunk);
    }

    qDebug() << "分割为" << chunks.size() << "个块";

    // 重组数据
    QByteArray reassembledData;
    for ( const QByteArray& chunk : chunks ) {
        reassembledData.append(chunk);
    }

    // 验证重组后的数据
    QCOMPARE(reassembledData.size(), originalData.size());
    QCOMPARE(reassembledData, originalData);

    // 验证重组后的校验和
    QByteArray reassembledChecksum = calculateChecksum(reassembledData);
    QCOMPARE(reassembledChecksum, originalChecksum);

    // 验证重组后的图像
    QImage reassembledImage;
    QVERIFY(reassembledImage.loadFromData(reassembledData, "PNG"));
    QVERIFY(compareImages(largeImage, reassembledImage));

    qDebug() << "大数据一致性验证通过";
}

void TestDataConsistency::test_multipleFrameConsistency() {
    qDebug() << "测试多帧数据一致性";

    // 创建连续帧序列
    QList<QImage> frameSequence;
    for ( int i = 0; i < 10; ++i ) {
        // 创建渐变色帧
        QImage frame(400, 300, QImage::Format_ARGB32);
        frame.fill(QColor(i * 25, 255 - i * 25, 128));
        frameSequence.append(frame);
    }

    // 处理每一帧并验证一致性
    QList<QByteArray> frameData;
    QList<QByteArray> frameChecksums;

    for ( int i = 0; i < frameSequence.size(); ++i ) {
        const QImage& frame = frameSequence[i];

        // 转换为字节数组
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(frame.save(&buffer, "PNG"));

        frameData.append(data);
        frameChecksums.append(calculateChecksum(data));
    }

    // 验证每帧的一致性
    for ( int i = 0; i < frameSequence.size(); ++i ) {
        // 从字节数组重建图像
        QImage reconstructedFrame;
        QVERIFY(reconstructedFrame.loadFromData(frameData[i], "PNG"));

        // 验证图像一致性
        QVERIFY(compareImages(frameSequence[i], reconstructedFrame));

        // 验证校验和一致性
        QByteArray verifyChecksum = calculateChecksum(frameData[i]);
        QCOMPARE(verifyChecksum, frameChecksums[i]);
    }

    // 验证帧序列的唯一性（每帧应该不同）
    for ( int i = 0; i < frameChecksums.size() - 1; ++i ) {
        for ( int j = i + 1; j < frameChecksums.size(); ++j ) {
            QVERIFY2(frameChecksums[i] != frameChecksums[j],
                qPrintable(QString("帧 %1 和帧 %2 的校验和相同").arg(i).arg(j)));
        }
    }

    qDebug() << "多帧数据一致性验证通过，测试了" << frameSequence.size() << "帧";
}

QImage TestDataConsistency::createTestImage(int width, int height, const QColor& color) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(color);

    // 添加一些图案以增加复杂性
    QPainter painter(&image);
    painter.setPen(QPen(Qt::black, 2));
    painter.drawRect(10, 10, width - 20, height - 20);
    painter.drawLine(0, 0, width, height);
    painter.drawLine(width, 0, 0, height);

    // 添加文本
    painter.setFont(QFont("Arial", 12));
    painter.drawText(width / 2 - 50, height / 2, QString("%1x%2").arg(width).arg(height));

    return image;
}

QByteArray TestDataConsistency::calculateChecksum(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

bool TestDataConsistency::compareImages(const QImage& img1, const QImage& img2) {
    if ( img1.size() != img2.size() ) {
        return false;
    }

    if ( img1.format() != img2.format() ) {
        // 转换为相同格式再比较
        QImage converted1 = img1.convertToFormat(QImage::Format_ARGB32);
        QImage converted2 = img2.convertToFormat(QImage::Format_ARGB32);
        return converted1 == converted2;
    }

    return img1 == img2;
}

void TestDataConsistency::waitForSignal(QObject* sender, const char* signal, int timeout) {
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(timeout);

    connect(sender, signal, m_eventLoop, SLOT(quit()));
    connect(&timer, &QTimer::timeout, m_eventLoop, &QEventLoop::quit);

    timer.start();
    m_eventLoop->exec();

    timer.stop();
    disconnect(sender, signal, m_eventLoop, SLOT(quit()));
    disconnect(&timer, &QTimer::timeout, m_eventLoop, &QEventLoop::quit);
}

QTEST_MAIN(TestDataConsistency)
#include "test_data_consistency.moc"