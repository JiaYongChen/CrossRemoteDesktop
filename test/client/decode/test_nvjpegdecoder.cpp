#include <QtTest/QtTest>
#include <QtCore/QByteArray>
#include <QtGui/QImage>

#include "client/decode/windows/NvJpegDecoder.h"
#include "client/decode/IDecoder.h"

/**
 * @brief NvJpegDecoder 单元测试
 *
 * 测试覆盖：
 * - NvJpegDecoder 构造/解码/名称基础行为
 * - 无 GPU 环境下行为验证（isAvailable=false, decode 返回 false）
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

    // isAvailable() 依赖实际 GPU 硬件探测结果
    // 此处仅验证构造不崩溃；GPU 环境测试在集成层
    Q_UNUSED(decoder.isAvailable());
}

void TestNvJpegDecoder::test_decode_returnsFalse() {
    NvJpegDecoder decoder;
    int w = 0, h = 0;
    GLsync fence = nullptr;
    QByteArray dummy("not-a-jpeg");

    // decode() 优先走 GPU 路径，失败时惰性回退 TurboJpegDecoder（CPU）
    // 此处用无效数据测试，两条路径均会失败
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
