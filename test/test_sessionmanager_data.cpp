#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include "test_sessionmanager_common.h"
#include "../src/client/managers/SessionManager.h"
#include "../src/common/core/network/Protocol.h"

class TestSessionManagerData : public QObject {
    Q_OBJECT

private:
    QByteArray makeScreenDataPacket(const QByteArray& jpegData) {
        ScreenData sd;
        sd.width = 640;
        sd.height = 480;
        sd.imageData = jpegData;
        sd.dataSize = static_cast<quint32>(jpegData.size());
        sd.flags = 0;
        return sd.encode();
    }

    QByteArray makeScaledScreenDataPacket(const QByteArray& jpegData,
                                           quint16 origW, quint16 origH) {
        ScreenData sd;
        sd.width = 320;    // 缩放后尺寸
        sd.height = 240;
        sd.imageData = jpegData;
        sd.dataSize = static_cast<quint32>(jpegData.size());
        sd.flags = static_cast<quint8>(ScreenDataFlags::SCALED);
        sd.originalWidth = origW;
        sd.originalHeight = origH;
        return sd.encode();
    }

    QByteArray m_validJpeg = QByteArray::fromHex("FFD8FFE000104A4649460001");

private slots:
    void test_handleScreenData_validJpeg() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockConnected = true;
        mock->m_mockAuthenticated = true;

        QByteArray pkt = makeScreenDataPacket(m_validJpeg);
        sm.onMessageReceived(MessageType::SCREEN_DATA, pkt);

        QSignalSpy errSpy(&sm, &SessionManager::sessionError);
        QCOMPARE(errSpy.count(), 0);
    }

    void test_handleScreenData_invalidJpegHeader() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockConnected = true;
        mock->m_mockAuthenticated = true;

        QByteArray badJpeg = QByteArray::fromHex("0000FFE000104A4649460001");
        QByteArray pkt = makeScreenDataPacket(badJpeg);
        sm.onMessageReceived(MessageType::SCREEN_DATA, pkt);

        QSignalSpy errSpy(&sm, &SessionManager::sessionError);
        QCOMPARE(errSpy.count(), 0);
    }

    void test_handleScreenData_emptyImage() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockConnected = true;
        mock->m_mockAuthenticated = true;

        QByteArray pkt = makeScreenDataPacket(QByteArray());
        sm.onMessageReceived(MessageType::SCREEN_DATA, pkt);

        QSignalSpy errSpy(&sm, &SessionManager::sessionError);
        QCOMPARE(errSpy.count(), 0);
    }

    void test_handleScreenData_disconnected() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockConnected = false;

        QByteArray pkt = makeScreenDataPacket(m_validJpeg);
        sm.onMessageReceived(MessageType::SCREEN_DATA, pkt);
        // 通过：无崩溃
    }

    void test_onMessageReceived_CURSOR_POSITION() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);

        CursorMessage msg;
        msg.cursorType = Qt::ArrowCursor;
        QByteArray data = msg.encode();

        QSignalSpy spy(&sm, &SessionManager::remoteCursorTypeUpdated);
        sm.onMessageReceived(MessageType::CURSOR_POSITION, data);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<Qt::CursorShape>(), Qt::ArrowCursor);
    }

    void test_onMessageReceived_CLIPBOARD_DATA_text() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);

        ClipboardMessage msg("hello clipboard");
        QByteArray data = msg.encode();

        QSignalSpy spy(&sm, &SessionManager::clipboardTextReceived);
        sm.onMessageReceived(MessageType::CLIPBOARD_DATA, data);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("hello clipboard"));
    }

    void test_onMessageReceived_CLIPBOARD_DATA_image() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);

        QByteArray imgData(100, '\x42');
        ClipboardMessage msg(imgData, 64, 64);
        QByteArray data = msg.encode();

        QSignalSpy spy(&sm, &SessionManager::clipboardImageReceived);
        sm.onMessageReceived(MessageType::CLIPBOARD_DATA, data);
        QCOMPARE(spy.count(), 1);
    }

    void test_onMessageReceived_unknownType() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);

        sm.onMessageReceived(static_cast<MessageType>(0xFFFF), QByteArray());
        // 通过：无崩溃
    }

    void test_handleCursorPosition_roundTrip() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);

        // 编码→解码往返验证
        CursorMessage msg(Qt::WaitCursor);
        QByteArray encoded = msg.encode();
        CursorMessage decoded;
        QVERIFY(decoded.decode(encoded));
        QCOMPARE(decoded.cursorType, Qt::WaitCursor);
    }

    void test_handleScreenData_scaled_updates_size() {
        MockConnectionManager* mock = new MockConnectionManager();
        SessionManager sm("test", mock);
        mock->m_mockConnected = true;
        mock->m_mockAuthenticated = true;

        QByteArray pkt = makeScaledScreenDataPacket(m_validJpeg, 1920, 1080);
        sm.onMessageReceived(MessageType::SCREEN_DATA, pkt);
        QCOMPARE(sm.remoteScreenSize(), QSize(1920, 1080));
    }
};

QTEST_MAIN(TestSessionManagerData)
#include "test_sessionmanager_data.moc"
