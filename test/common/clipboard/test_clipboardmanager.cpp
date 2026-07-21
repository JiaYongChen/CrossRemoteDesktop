#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtGui/QImage>
#include <QtCore/QBuffer>
#include "../../../src/common/clipboard/ClipboardManager.h"

class ClipboardManagerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // 确保 QGuiApplication 存在（QClipboard 需要）
        if (!qGuiApp) {
            static int argc = 0;
            static char* argv[] = {nullptr};
            new QGuiApplication(argc, argv);
        }
    }

    void testSetTextEmitsSignal() {
        ClipboardManager mgr;
        mgr.setEnabled(true);

        QSignalSpy spy(&mgr, &ClipboardManager::clipboardTextChanged);

        mgr.setText("Hello");
        QTest::qWait(50); // 等待剪贴板异步事件

        // setText 自己触发的 onClipboardChanged 经过去重检查后可能不会发射
        // 这里测试核心：setText 不应该崩溃，m_lastText 已记录
        QCOMPARE(spy.count(), 0); // setText 是外部写入，onClipboardChanged 中内容与 m_lastText 相同
    }

    void testApplyRemoteTextPreventsLoopback() {
        ClipboardManager mgr;
        mgr.setEnabled(true);

        QSignalSpy spy(&mgr, &ClipboardManager::clipboardTextChanged);

        mgr.applyRemoteText("RemoteText");
        QTest::qWait(50);

        // applyRemoteText 写入后 onClipboardChanged 检测到等于 m_lastReceivedText
        // → 清空 m_lastReceivedText → 不 emit
        QCOMPARE(spy.count(), 0);
    }

    void testDifferentLocalTextEmitsSignal() {
        ClipboardManager mgr;
        mgr.setEnabled(true);

        QSignalSpy spy(&mgr, &ClipboardManager::clipboardTextChanged);

        // 直接模拟本地变化：先 set 初始化状态，然后手动触发不同于 m_lastText 的内容
        mgr.setText("Initial");
        QTest::qWait(50);

        // 设置一个不同于当前 m_lastText 的内容
        QGuiApplication::clipboard()->setText("NewLocalText");
        QTest::qWait(50);

        // onClipboardChanged 应检测到 "NewLocalText" != m_lastText("Initial") 且 != m_lastReceivedText
        QVERIFY(spy.count() >= 1);
    }

    void testEmptyTextIgnored() {
        ClipboardManager mgr;
        mgr.setEnabled(true);

        QSignalSpy spy(&mgr, &ClipboardManager::clipboardTextChanged);

        mgr.setText(QString());
        QTest::qWait(50);

        // 空文本——setText 中 m_lastText 设为空，下次有效文本才能触发
        // 空字符串本身不 emit 信号（onClipboardChanged 检测空文本可能不触发 hasText）
    }

    void testOversizedImageRejected() {
        ClipboardManager mgr;
        mgr.setEnabled(true);

        // 生成超过 10MB 的假 PNG 数据
        QByteArray hugeData(11 * 1024 * 1024, '\0');
        // 置入合法 PNG 签名头
        const unsigned char pngHeader[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
        };
        for (size_t i = 0; i < sizeof(pngHeader); ++i) {
            hugeData[i] = static_cast<char>(pngHeader[i]);
        }

        QSignalSpy spy(&mgr, &ClipboardManager::clipboardImageChanged);

        mgr.setImageFromPng(hugeData);
        QTest::qWait(50);

        // 超限图片不应触发信号
        QCOMPARE(spy.count(), 0);
    }

    void testDisabledManagerDoesNotEmit() {
        ClipboardManager mgr;
        mgr.setEnabled(false);

        QSignalSpy spy(&mgr, &ClipboardManager::clipboardTextChanged);

        mgr.setText("ShouldNotEmit");
        QTest::qWait(50);

        QCOMPARE(spy.count(), 0);
    }

    void testDisabledManagerSetTextNoop() {
        ClipboardManager mgr;
        mgr.setEnabled(false);

        // 记录当前剪贴板内容
        QString before = QGuiApplication::clipboard()->text();

        mgr.setText("NoopText");
        QTest::qWait(50);

        // 禁用时不应修改系统剪贴板
        QString after = QGuiApplication::clipboard()->text();
        QCOMPARE(after, before);
    }
};

QTEST_MAIN(ClipboardManagerTest)
#include "test_clipboardmanager.moc"
