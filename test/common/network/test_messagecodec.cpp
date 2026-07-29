#include <QtTest/QTest>
#include "common/network/Protocol.h"
#include "common/config/ProtocolConstants.h"

class TestMessageCodec : public QObject {
    Q_OBJECT
private slots:
    // ── SessionCapabilities ──
    void sessionCapabilities_roundtrip() {
        SessionCapabilities src;
        src.imageQuality = 75;
        src.colorDepth = 24;

        const QByteArray bytes = src.encode();
        SessionCapabilities dst;
        QVERIFY(dst.decode(bytes));
        QCOMPARE(dst.imageQuality, src.imageQuality);
        QCOMPARE(dst.colorDepth, src.colorDepth);
    }

    void sessionCapabilities_decode_emptyBuffer_fails() {
        SessionCapabilities caps;
        QVERIFY(!caps.decode(QByteArray()));
    }

    void sessionCapabilities_decode_truncatedBuffer_fails() {
        SessionCapabilities src;
        src.imageQuality = 50;
        src.colorDepth = 16;
        const QByteArray bytes = src.encode();

        SessionCapabilities dst;
        QVERIFY(!dst.decode(bytes.left(bytes.size() - 1))); // 截断一字节
    }
};

QTEST_MAIN(TestMessageCodec)
#include "test_messagecodec.moc"
