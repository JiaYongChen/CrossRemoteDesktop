#include <QtTest/QtTest>
#include "../src/common/core/protocolcodec.h"
#include <QCryptographicHash>

class TestProtocolCodec : public QObject {
    Q_OBJECT
private slots:
    void encodeDecode_basic() {
        ProtocolCodec codec;
        QByteArray payload("hello", 5);
        QByteArray frame = codec.encode(MessageType::STATUS_UPDATE, payload);
        QVERIFY(frame.size() > 0);

        QByteArray buffer = frame;
        MessageHeader header;
        QByteArray out;
        bool ok = codec.tryDecode(buffer, header, out);
        QVERIFY(ok);
        QCOMPARE(static_cast<quint32>(header.type), static_cast<quint32>(MessageType::STATUS_UPDATE));
        QCOMPARE(out, payload);
        QCOMPARE(buffer.size(), 0);
    }

    void tryDecode_resyncMagic() {
        ProtocolCodec codec;
        QByteArray payload("world", 5);
        QByteArray frame = codec.encode(MessageType::ERROR_MESSAGE, payload);
        QByteArray garbage("xxxx", 4);
        QByteArray buffer = garbage + frame; // 前置垃圾数据

        MessageHeader header;
        QByteArray out;

        // 第一次应返回false并清理部分前缀
        bool ok1 = codec.tryDecode(buffer, header, out);
        QVERIFY(!ok1);
        QVERIFY(buffer.size() < (garbage.size() + frame.size()));

        // 再尝试一次应成功
        bool ok2 = codec.tryDecode(buffer, header, out);
        QVERIFY(ok2);
        QCOMPARE(static_cast<quint32>(header.type), static_cast<quint32>(MessageType::ERROR_MESSAGE));
        QCOMPARE(out, payload);
    }

    void decode_multipleConcatenatedFrames() {
        ProtocolCodec codec;
        QByteArray p1("A", 1), p2("BC", 2);
        QByteArray f1 = codec.encode(MessageType::STATUS_UPDATE, p1);
        QByteArray f2 = codec.encode(MessageType::ERROR_MESSAGE, p2);
        QByteArray buffer = f1 + f2;

        MessageHeader h; QByteArray out;
        QVERIFY(codec.tryDecode(buffer, h, out));
        QCOMPARE(static_cast<quint32>(h.type), static_cast<quint32>(MessageType::STATUS_UPDATE));
        QCOMPARE(out, p1);

        QVERIFY(codec.tryDecode(buffer, h, out));
        QCOMPARE(static_cast<quint32>(h.type), static_cast<quint32>(MessageType::ERROR_MESSAGE));
        QCOMPARE(out, p2);
        QCOMPARE(buffer.size(), 0);
    }

    void decode_partialThenComplete() {
        ProtocolCodec codec;
        QByteArray payload("hello-partial", 13);
        QByteArray frame = codec.encode(MessageType::STATUS_UPDATE, payload);
        // 切成两段：前半段不足以完整解析
        int cut = qMax(1, frame.size() / 2);
        QByteArray buffer = frame.left(cut);

        MessageHeader h; QByteArray out;
        QVERIFY(!codec.tryDecode(buffer, h, out));
        // 补齐剩余数据
        buffer += frame.mid(cut);
        QVERIFY(codec.tryDecode(buffer, h, out));
        QCOMPARE(static_cast<quint32>(h.type), static_cast<quint32>(MessageType::STATUS_UPDATE));
        QCOMPARE(out, payload);
        QCOMPARE(buffer.size(), 0);
    }

    void auth_fieldwise_roundtrip() {
        QString user = "alice";
        QString ph = "abcd0123deadbeef"; // 伪hash
        QByteArray req = Protocol::encodeAuthenticationRequest(user, ph, 0u);
        AuthenticationRequest r{};
        QVERIFY(Protocol::decodeAuthenticationRequest(req, r));
        QCOMPARE(QString::fromUtf8(r.username), user);
        QCOMPARE(QString::fromUtf8(r.passwordHash), ph);
        QCOMPARE(r.authMethod, 0u);

        QByteArray resp = Protocol::encodeAuthenticationResponse(AuthResult::SUCCESS, "sess-1", 7u);
        AuthenticationResponse rr{};
        QVERIFY(Protocol::decodeAuthenticationResponse(resp, rr));
        QCOMPARE(static_cast<int>(rr.result), static_cast<int>(AuthResult::SUCCESS));
        QCOMPARE(QString::fromUtf8(rr.sessionId), QString("sess-1"));
        QCOMPARE(rr.permissions, 7u);
    }

    void inputEvents_fieldwise_roundtrip() {
        // MouseEvent
        MouseEvent m{}; m.eventType = MouseEventType::MOVE; m.x = 100; m.y = 200; m.buttons = 3; m.wheelDelta = 0;
        QByteArray mb = Protocol::encodeMouseEvent(m);
        MouseEvent mout{}; QVERIFY(Protocol::decodeMouseEvent(mb, mout));
        QCOMPARE(static_cast<int>(mout.eventType), static_cast<int>(MouseEventType::MOVE));
        QCOMPARE(mout.x, 100); QCOMPARE(mout.y, 200); QCOMPARE(int(mout.buttons), 3); QCOMPARE(mout.wheelDelta, 0);

        // KeyboardEvent
        KeyboardEvent k{}; k.eventType = KeyboardEventType::KEY_PRESS; k.keyCode = 65; k.modifiers = 2; memset(k.text, 0, sizeof(k.text)); qstrncpy(k.text, "a", sizeof(k.text)-1);
        QByteArray kb = Protocol::encodeKeyboardEvent(k);
        KeyboardEvent kout{}; QVERIFY(Protocol::decodeKeyboardEvent(kb, kout));
        QCOMPARE(static_cast<int>(kout.eventType), static_cast<int>(KeyboardEventType::KEY_PRESS));
        QCOMPARE(kout.keyCode, (quint32)65); QCOMPARE(kout.modifiers, (quint32)2);
        QCOMPARE(QString::fromUtf8(kout.text), QString("a"));

        // ErrorMessage
        QByteArray eb = Protocol::encodeErrorMessage(1234u, "Oops");
        ErrorMessage eout{}; QVERIFY(Protocol::decodeErrorMessage(eb, eout));
        QCOMPARE(eout.errorCode, (quint32)1234);
        QCOMPARE(QString::fromUtf8(eout.errorText), QString("Oops"));
    }

    void statusUpdate_fieldwise_roundtrip() {
        StatusUpdate s{}; s.connectionStatus = 1; s.bytesReceived = 1000; s.bytesSent = 2000; s.fps = 60; s.cpuUsage = 23; s.memoryUsage = 4096;
        QByteArray b = Protocol::encodeStatusUpdate(s);
        StatusUpdate out{}; QVERIFY(Protocol::decodeStatusUpdate(b, out));
        QCOMPARE((int)out.connectionStatus, 1);
        QCOMPARE(out.bytesReceived, (quint32)1000);
        QCOMPARE(out.bytesSent, (quint32)2000);
        QCOMPARE(out.fps, (quint16)60);
        QCOMPARE((int)out.cpuUsage, 23);
        QCOMPARE(out.memoryUsage, (quint32)4096);
    }

    void fileTransfer_and_clipboard_roundtrip() {
        // FileTransferRequest
        FileTransferRequest r{}; memset(&r, 0, sizeof(r));
        qstrncpy(r.fileName, "report.pdf", sizeof(r.fileName)-1);
        r.fileSize = 123456789ULL; r.transferId = 42; r.direction = 1;
        QByteArray rb = Protocol::encodeFileTransferRequest(r);
        FileTransferRequest rout{}; QVERIFY(Protocol::decodeFileTransferRequest(rb, rout));
        QCOMPARE(QString::fromUtf8(rout.fileName), QString("report.pdf"));
        QCOMPARE(rout.fileSize, (quint64)123456789ULL);
        QCOMPARE(rout.transferId, (quint32)42);
        QCOMPARE((int)rout.direction, 1);

        // FileTransferResponse
        FileTransferResponse resp{}; resp.transferId = 42; resp.status = FileTransferStatus::IN_PROGRESS; memset(resp.errorMessage, 0, sizeof(resp.errorMessage));
        qstrncpy(resp.errorMessage, "", sizeof(resp.errorMessage)-1);
        QByteArray respb = Protocol::encodeFileTransferResponse(resp);
        FileTransferResponse respOut{}; QVERIFY(Protocol::decodeFileTransferResponse(respb, respOut));
        QCOMPARE(respOut.transferId, (quint32)42);
        QCOMPARE(static_cast<int>(respOut.status), static_cast<int>(FileTransferStatus::IN_PROGRESS));
        QCOMPARE(QString::fromUtf8(respOut.errorMessage), QString(""));

        // FileData
        FileData fh{}; fh.transferId = 42; fh.offset = 4096; fh.dataSize = 5;
        QByteArray fdata("abcde", 5);
        QByteArray fbytes = Protocol::encodeFileData(fh, fdata);
        FileData fhOut{}; QByteArray fdOut;
        QVERIFY(Protocol::decodeFileData(fbytes, fhOut, fdOut));
        QCOMPARE(fhOut.transferId, (quint32)42); QCOMPARE(fhOut.offset, (quint64)4096); QCOMPARE(fhOut.dataSize, (quint32)5);
        QCOMPARE(fdOut, fdata);

        // ClipboardData
        QByteArray clip("hello", 5);
        QByteArray clipb = Protocol::encodeClipboardData(0, clip);
        ClipboardData clipMeta{}; QByteArray clipOut;
        QVERIFY(Protocol::decodeClipboardData(clipb, clipMeta, clipOut));
        QCOMPARE((int)clipMeta.dataType, 0);
        QCOMPARE(clipMeta.dataSize, (quint32)5);
        QCOMPARE(clipOut, clip);
    }

    void auth_challenge_end_to_end() {
        // 服务端生成参数
        quint32 method = 1; quint32 iters = 100000; quint32 keyLen = 32;
        QByteArray salt(16, 0); for (int i=0;i<salt.size();++i) salt[i] = char(i+1);
        QByteArray challenge = Protocol::encodeAuthChallenge(method, iters, keyLen, salt);
        AuthChallenge ch{}; QVERIFY(Protocol::decodeAuthChallenge(challenge, ch));
        QCOMPARE(ch.method, method); QCOMPARE(ch.iterations, iters); QCOMPARE(ch.keyLength, keyLen);
        QByteArray saltHex = QByteArray(ch.saltHex);
        QByteArray saltBin = QByteArray::fromHex(saltHex);
        QCOMPARE(saltBin, salt);

    // 客户端本地派生（测试中用 SHA-256 模拟，长度恰为32字节）
    QString password = "P@ssw0rd";
    QByteArray clientDeriv = QCryptographicHash::hash(password.toUtf8() + saltBin, QCryptographicHash::Sha256);
        QString clientHex = clientDeriv.toHex();

        // 客户端回传认证请求（用户名+派生值hex）
        QByteArray req = Protocol::encodeAuthenticationRequest("alice", clientHex, 1u);
        AuthenticationRequest ar{}; QVERIFY(Protocol::decodeAuthenticationRequest(req, ar));
        QCOMPARE(QString::fromUtf8(ar.username), QString("alice"));
        // 服务端用相同参数验证（常量时间模拟）
    QByteArray provided = QByteArray::fromHex(QByteArray(ar.passwordHash));
    QByteArray expected = QByteArray::fromHex(clientHex.toUtf8());
    QCOMPARE(provided, expected);
    }
};

QTEST_APPLESS_MAIN(TestProtocolCodec)
#include "test_protocol_codec.moc"

