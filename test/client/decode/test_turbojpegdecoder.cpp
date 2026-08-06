#include <QtCore/QBuffer>
#include <QtGui/QImage>
#include <QtTest/QTest>

#include "client/decode/TurboJpegDecoder.h"

/**
 * @brief TurboJpegDecoder 单元测试
 *
 * 测试覆盖：
 * - 有效 JPEG 解码正确性（尺寸输出 + 回退 QImage）
 * - 无效数据 / 空数据返回 false
 * - 多帧连续解码无状态泄漏（缓冲复用）
 */
class TurboJpegDecoderTest : public QObject {
    Q_OBJECT

private:
    /// @brief 生成指定尺寸的合法 JPEG 数据（红色纯色图像）
    static QByteArray makeValidJpeg(int width = 16, int height = 16)
    {
        QImage img(width, height, QImage::Format_RGB888);
        img.fill(Qt::red);
        QByteArray data;
        QBuffer buf(&data);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "JPEG", 90);
        buf.close();
        return data;
    }

private slots:
    void decode_validJpeg_producesCorrectDimensions()
    {
        TurboJpegDecoder decoder;
        const QByteArray jpeg = makeValidJpeg(32, 24);

        int w = 0, h = 0;
        GLsync fence = nullptr;
        QImage outImage;
        const bool ok = decoder.decode(jpeg, &w, &h, &fence, &outImage);

        QVERIFY(ok);
        QCOMPARE(w, 32);
        QCOMPARE(h, 24);
        QVERIFY(fence == nullptr);
        QVERIFY(!outImage.isNull());
        QCOMPARE(outImage.width(), 32);
        QCOMPARE(outImage.height(), 24);
    }

    void decode_invalidData_returnsFalse()
    {
        TurboJpegDecoder decoder;
        const QByteArray garbage = QByteArray(64, '\x00');

        int w = 0, h = 0;
        GLsync fence = nullptr;
        QImage outImage;
        const bool ok = decoder.decode(garbage, &w, &h, &fence, &outImage);

        QVERIFY(!ok);
    }

    void decode_emptyData_returnsFalse()
    {
        TurboJpegDecoder decoder;
        const QByteArray empty;

        int w = 0, h = 0;
        GLsync fence = nullptr;
        QImage outImage;
        const bool ok = decoder.decode(empty, &w, &h, &fence, &outImage);

        QVERIFY(!ok);
    }

    void decode_multipleFrames_noStateLeak()
    {
        TurboJpegDecoder decoder;

        QByteArray jpeg1 = makeValidJpeg(32, 32);
        int w1 = 0, h1 = 0;
        GLsync f1 = nullptr;
        QImage img1;
        QVERIFY(decoder.decode(jpeg1, &w1, &h1, &f1, &img1));
        QCOMPARE(w1, 32);
        QCOMPARE(h1, 32);

        QByteArray jpeg2 = makeValidJpeg(64, 48);
        int w2 = 0, h2 = 0;
        GLsync f2 = nullptr;
        QImage img2;
        QVERIFY(decoder.decode(jpeg2, &w2, &h2, &f2, &img2));
        QCOMPARE(w2, 64);
        QCOMPARE(h2, 48);

        QCOMPARE(img1.width(), 32);
        QCOMPARE(img1.height(), 32);
        QCOMPARE(img2.width(), 64);
        QCOMPARE(img2.height(), 48);
    }
};

QTEST_MAIN(TurboJpegDecoderTest)
#include "test_turbojpegdecoder.moc"
