#include <QtTest/QTest>
#include <QtTest/QSignalSpy>

#include "client/session/ProtocolSession.h"
#include "client/session/DecodePipeline.h"
#include "client/network/ConnectionManager.h"

/// 可控的 ConnectionManager 测试替身：覆写 virtual 查询/发送接口，
/// 并提供 injectMessage 从测试侧模拟服务端消息到达
class MockConnectionManager : public ConnectionManager {
public:
    explicit MockConnectionManager(QObject* parent = nullptr)
        : ConnectionManager(parent) {}

    bool isConnected() const override { return mockConnected; }
    bool isAuthenticated() const override { return mockAuthenticated; }
    void sendMessage(MessageType type, const IMessageCodec& message) override {
        Q_UNUSED(message);
        sentTypes.append(type);
    }

    /// 触发 ProtocolSession::onMessageReceived（消息路由入口）
    void injectMessage(MessageType type, const QByteArray& payload) {
        emit messageReceived(type, payload);
    }

    bool mockConnected = false;
    bool mockAuthenticated = false;
    QList<MessageType> sentTypes;
};

/**
 * @brief ProtocolSession 单元测试
 *
 * 覆盖：输入事件激活门控、剪贴板消息路由、
 * SCALED 屏幕数据的远程尺寸更新。
 */
class TestProtocolSession : public QObject {
    Q_OBJECT

private slots:
    /// 会话未激活时输入事件不得发送到网络层
    void inactiveSession_dropsInputEvents() {
        MockConnectionManager mock;
        ProtocolSession ps(&mock, nullptr);

        ps.sendMouseEvent(10, 20, 1);
        ps.sendKeyboardEvent(65, 0, true, QStringLiteral("a"));
        ps.sendWheelEvent(10, 20, 120, 1);

        QVERIFY(mock.sentTypes.isEmpty());
    }

    /// 认证 + 管线运行后，输入事件正常序列化发送
    void activeSession_forwardsInputEvents() {
        MockConnectionManager mock;
        mock.mockAuthenticated = true;
        DecodePipeline pipeline(QStringLiteral("test-conn"));
        ProtocolSession ps(&mock, &pipeline);

        ps.startSession();
        QVERIFY(ps.isActive());

        ps.sendMouseEvent(10, 20, 1);
        ps.sendKeyboardEvent(65, 0, true, QStringLiteral("a"));

        QVERIFY(mock.sentTypes.contains(MessageType::MOUSE_EVENT));
        QVERIFY(mock.sentTypes.contains(MessageType::KEYBOARD_EVENT));

        pipeline.stop();
        QVERIFY(!ps.isActive());  // 管线停止后会话立即失活（close 流程窗口期守卫）
    }

    /// CLIPBOARD_DATA 文本消息路由到 clipboardTextReceived 信号
    void clipboardData_routedToSignal() {
        MockConnectionManager mock;
        ProtocolSession ps(&mock, nullptr);
        QSignalSpy textSpy(&ps, &ProtocolSession::clipboardTextReceived);
        QVERIFY(textSpy.isValid());

        ClipboardMessage msg(QStringLiteral("hello"));
        mock.injectMessage(MessageType::CLIPBOARD_DATA, msg.encode());

        QCOMPARE(textSpy.count(), 1);
        QCOMPARE(textSpy.at(0).at(0).toString(), QStringLiteral("hello"));
    }

    /// SCALED 屏幕帧携带 originalWidth/Height 时更新远程屏幕尺寸并发射信号
    void scaledScreenData_updatesRemoteSize() {
        MockConnectionManager mock;
        mock.mockConnected = true;  // SCREEN_DATA 路由要求已连接
        ProtocolSession ps(&mock, nullptr);
        QSignalSpy sizeSpy(&ps, &ProtocolSession::remoteScreenSizeChanged);
        QVERIFY(sizeSpy.isValid());

        ScreenData sd{};
        sd.width = 960;
        sd.height = 540;
        sd.flags = static_cast<quint8>(ScreenDataFlags::SCALED);
        sd.originalWidth = 1920;
        sd.originalHeight = 1080;
        // 合法 JPEG 头（SOI: FF D8）+ 2 字节占位数据
        const QByteArray jpeg = QByteArray::fromHex("FFD87878");
        sd.imageData = jpeg;
        sd.dataSize = static_cast<quint32>(jpeg.size());

        mock.injectMessage(MessageType::SCREEN_DATA, sd.encode());

        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(ps.remoteScreenSize(), QSize(1920, 1080));
    }
};

QTEST_MAIN(TestProtocolSession)
#include "test_protocolsession.moc"
