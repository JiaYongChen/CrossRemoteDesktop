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

    // ── HandshakeRequest（裁剪后：仅 version/name/OS）──
    void handshakeRequest_roundtrip() {
        HandshakeRequest src;
        src.clientVersion = ProtocolConstants::ProtocolVersion;
        src.clientName = QStringLiteral("UltraDesktop Client");
        src.clientOS = QStringLiteral("Windows");

        const QByteArray bytes = src.encode();
        HandshakeRequest dst;
        QVERIFY(dst.decode(bytes));
        QCOMPARE(dst.clientVersion, src.clientVersion);
        QCOMPARE(dst.clientName, src.clientName);
        QCOMPARE(dst.clientOS, src.clientOS);
    }

    // ── HandshakeResponse（裁剪后：仅 version/name/OS）──
    void handshakeResponse_roundtrip() {
        HandshakeResponse src;
        src.serverVersion = ProtocolConstants::ProtocolVersion;
        src.serverName = QStringLiteral("UltraDesktop Server");
        src.serverOS = QStringLiteral("Windows");

        const QByteArray bytes = src.encode();
        HandshakeResponse dst;
        QVERIFY(dst.decode(bytes));
        QCOMPARE(dst.serverVersion, src.serverVersion);
        QCOMPARE(dst.serverName, src.serverName);
        QCOMPARE(dst.serverOS, src.serverOS);
    }
};

QTEST_MAIN(TestMessageCodec)
#include "test_messagecodec.moc"
