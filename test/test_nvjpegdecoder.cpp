#include <QtTest/QtTest>
#include <QtCore/QByteArray>
#include <QtGui/QImage>

#include "../src/client/decode/NvJpegDecoder.h"
#include "../src/client/decode/IDecoder.h"

/**
 * @brief NvJpegDecoder 单元测试
 *
 * 测试覆盖：
 * - 编译路径（HAS_NVJPEG / !HAS_NVJPEG）
 * - 无 GPU 环境下 stub 行为
 */
class TestNvJpegDecoder : public QObject {
    Q_OBJECT

private slots:
    /// @brief 构造后 isAvailable() 应正确反映可用性
    void test_constructor_availability();

    /// @brief decode() 始终返回 false
    void test_decode_returnsFalse();

    /// @brief decodeToPBO() 不可用时返回 false
    void test_decodeToPBO_unavailable_returnsFalse();

    /// @brief decodeToPBO() 无效数据时返回 false
    void test_decodeToPBO_invalidJpeg_returnsFalse();

    /// @brief name() 返回 "nvJPEG"
    void test_name_returnsNvJpeg();
};

void TestNvJpegDecoder::test_constructor_availability() {
    NvJpegDecoder decoder;

#ifdef HAS_NVJPEG
    // 有 CUDA SDK + GPU 时 isAvailable() 应依赖实际硬件
    // 此处仅验证不崩溃；GPU 环境测试在集成层
    Q_UNUSED(decoder.isAvailable());
#else
    // 无 CUDA SDK：必然不可用
    QVERIFY(!decoder.isAvailable());
#endif
}

void TestNvJpegDecoder::test_decode_returnsFalse() {
    NvJpegDecoder decoder;
    QImage img;
    int w = 0, h = 0;
    QByteArray dummy("not-a-jpeg");

    // decode() 始终返回 false（无 CPU 路径）
    QVERIFY(!decoder.decode(dummy, img, &w, &h));
}

void TestNvJpegDecoder::test_decodeToPBO_unavailable_returnsFalse() {
#ifndef HAS_NVJPEG
    NvJpegDecoder decoder;
    QByteArray dummy;

    // 不可用时 decodeToPBO() 应立即返回 false
    QVERIFY(!decoder.decodeToPBO(dummy, nullptr, 1920, 1080, 3));
#else
    // 有 CUDA SDK 时依赖硬件，不做断言
    QVERIFY(true);
#endif
}

void TestNvJpegDecoder::test_decodeToPBO_invalidJpeg_returnsFalse() {
#ifdef HAS_NVJPEG
    NvJpegDecoder decoder;
    if (!decoder.isAvailable()) {
        QSKIP("No CUDA GPU available — skipping decode test");
    }

    // 随机字节不是有效 JPEG — 应返回 false
    QByteArray invalidData(1024, '\x00');
    QVERIFY(!decoder.decodeToPBO(invalidData, nullptr, 1920, 1080, 3));
#else
    QVERIFY(true);
#endif
}

void TestNvJpegDecoder::test_name_returnsNvJpeg() {
    NvJpegDecoder decoder;
    QCOMPARE(QString::fromLatin1(decoder.name()), QStringLiteral("nvJPEG"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// 主函数
// ═══════════════════════════════════════════════════════════════════════════════

QTEST_MAIN(TestNvJpegDecoder)
#include "test_nvjpegdecoder.moc"
