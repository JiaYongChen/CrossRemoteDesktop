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

    // ── ClipboardMessage（整数溢出防 DoS）──
    void clipboardMessage_textRoundtrip() {
        ClipboardMessage src(QStringLiteral("hello clipboard"));
        const QByteArray bytes = src.encode();

        ClipboardMessage dst;
        QVERIFY(dst.decode(bytes));
        QVERIFY(dst.isText());
        QCOMPARE(dst.text(), QStringLiteral("hello clipboard"));
    }

    void clipboardMessage_textDecode_oversizedDataSize_fails() {
        // TEXT 载荷声称 dataSize=0xFFFFFFFF（实际仅 5 字节）。修复前 static_cast<int>
        // 溢出回绕使边界校验失效，data.resize(0xFFFFFFFF) 分配约 4GiB（远程单包 DoS）；
        // 修复后以 qsizetype 比较，required 远超缓冲区，decode 在 resize 前返回 false。
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out << static_cast<quint8>(0x01);   // ClipboardDataType::TEXT
        out << quint32(0xFFFFFFFF);         // 超限 dataSize

        ClipboardMessage msg;
        QVERIFY(!msg.decode(payload));
    }

    void clipboardMessage_imageDecode_oversizedDataSize_fails() {
        // IMAGE 载荷声称 dataSize=0xFFFFFFFF，同 TEXT 分支的溢出路径。
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out << static_cast<quint8>(0x02);   // ClipboardDataType::IMAGE
        out << quint32(1920);               // width
        out << quint32(1080);               // height
        out << quint32(0xFFFFFFFF);         // 超限 dataSize

        ClipboardMessage msg;
        QVERIFY(!msg.decode(payload));
    }

    // ── CursorMessage（pixelSize 有符号溢出防绕过）──
    void cursorMessage_roundtrip() {
        CursorMessage src(10, 20, 1, 2, 2, 2, QByteArray(16, '\x42'));  // 2×2 RGBA = 16 字节
        const QByteArray bytes = src.encode();

        CursorMessage dst;
        QVERIFY(dst.decode(bytes));
        QCOMPARE(dst.posX, 10);
        QCOMPARE(dst.posY, 20);
        QCOMPARE(dst.width, 2);
        QCOMPARE(dst.height, 2);
        QCOMPARE(dst.pixels, QByteArray(16, '\x42'));
    }

    void cursorMessage_decode_oversizedPixelSize_fails() {
        // pixelSize=0x7FFFFFFF（声称约 2GiB 像素，实际仅 4 字节）。修复前 28+pixelSize
        // 有符号溢出回绕为负，dataBuffer.size()<负值 恒不成立使校验被绕过，decode 返回 true；
        // 修复后以 qsizetype 计算，28+0x7FFFFFFF 远超缓冲区，decode 返回 false。
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out << qint32(0) << qint32(0) << qint32(0) << qint32(0);  // posX/posY/hotX/hotY
        out << qint32(2) << qint32(2);                            // width/height
        out << qint32(0x7FFFFFFF);                                // 超限 pixelSize
        const QByteArray fakePixels(4, '\x42');
        out.writeRawData(fakePixels.constData(), fakePixels.size());

        CursorMessage cursor;
        QVERIFY(!cursor.decode(payload));
    }

    // ── 解码层纵深防御（非法 UTF-8 / 尾部垃圾）──
    void handshakeRequest_decode_invalidUtf8_fails() {
        // clientName 含非法 UTF-8 字节(0xFF)。修复前静默替换为 U+FFFD 并 decode 成功；
        // 修复后 readPrefixedString 校验 UTF-8，非法内容置错误态，decode 返回 false。
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out << quint32(2);                  // clientVersion
        out << quint32(1);                  // clientName 长度=1
        const QByteArray badUtf8(1, '\xFF');  // 非法 UTF-8 字节
        out.writeRawData(badUtf8.constData(), badUtf8.size());
        out << quint32(0);                  // clientOS 空

        HandshakeRequest req;
        QVERIFY(!req.decode(payload));
    }

    void handshakeRequest_decode_trailingBytes_fails() {
        // 合法握手包尾部追加垃圾字节。修复前忽略尾部垃圾 decode 成功；
        // 修复后 decode 末尾 atEnd 检查拒绝尾部多余字节。
        HandshakeRequest src;
        src.clientVersion = ProtocolConstants::ProtocolVersion;
        src.clientName = QStringLiteral("UltraDesktop Client");
        src.clientOS = QStringLiteral("Windows");
        QByteArray payload = src.encode();
        payload.append('\x99');             // 尾部垃圾字节

        HandshakeRequest req;
        QVERIFY(!req.decode(payload));
    }
};

QTEST_MAIN(TestMessageCodec)
#include "test_messagecodec.moc"
