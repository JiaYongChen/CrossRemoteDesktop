#include <QtTest/QtTest>
#include <QtCore/QByteArray>
#include <QtGui/QImage>

#include "../../src/client/decode/NvJpegDecoder.h"
#include "../../src/client/decode/IDecoder.h"

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
    int w = 0, h = 0;
    GLsync fence = nullptr;
    QByteArray dummy("not-a-jpeg");

    // decode() 始终返回 false（无 CPU 路径）
    QVERIFY(!decoder.decode(dummy, &w, &h, &fence, nullptr));
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
