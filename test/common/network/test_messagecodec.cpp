#include <QtTest/QTest>
#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
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

    // ── 负向用例：畸形长度前缀必须被拒绝（钉死协议健壮性契约）──
    void handshakeRequest_decode_oversizedNameLength_fails() {
        // clientName 长度前缀 = 0xFFFFFFFF（远超 MaxHostnameLength=1024）。
        // readPrefixedString 修复前对此只返回空串、不置流错误也不消费字符串数据，
        // 随后 clientOS 把 name 数据前 4 字节当长度前缀误解析，decode 假成功返回 true。
        // 修复后应置 ReadCorruptData，decode 返回 false。
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out << quint32(2);            // clientVersion
        out << quint32(0xFFFFFFFF);   // clientName 长度前缀（超限）
        out << quint32(2);            // 会被误读为 clientOS 长度前缀
        out.writeRawData("AB", 2);    // 会被误读为 clientOS 数据

        HandshakeRequest req;
        QVERIFY(!req.decode(payload));   // 畸形包必须被拒绝
    }
};

QTEST_MAIN(TestMessageCodec)
#include "test_messagecodec.moc"
