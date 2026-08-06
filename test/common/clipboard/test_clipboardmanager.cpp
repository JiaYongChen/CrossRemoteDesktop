#include <QtCore/QBuffer>
#include <QtCore/QFile>
#include <QtCore/QMimeData>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include "common/clipboard/ClipboardManager.h"

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

        // 空文本不应触发剪贴板变化信号
        QCOMPARE(spy.count(), 0);
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

    void testResyncReemitsCurrentContent() {
        ClipboardManager mgr;
        mgr.setEnabled(true);

        // 建立当前内容基线（模拟认证前复制、被发送闸门丢弃后基线已推进的状态）
        mgr.setText("resync-me");
        QTest::qWait(50);

        QSignalSpy textSpy(&mgr, &ClipboardManager::clipboardTextChanged);
        QSignalSpy imageSpy(&mgr, &ClipboardManager::clipboardImageChanged);

        mgr.resync();

        // 认证成功后补发：当前文本内容必须重新发射
        QCOMPARE(textSpy.count(), 1);
        QCOMPARE(textSpy.at(0).at(0).toString(), QString("resync-me"));
        QCOMPARE(imageSpy.count(), 0);
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

    void testFileListDeDupPreventsRepeatSignal() {
        ClipboardManager mgr;
        mgr.setEnabled(true);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = createTempFile(dir, "dedup.txt", "hello");
        QVERIFY(!filePath.isEmpty());

        QSignalSpy spy(&mgr, &ClipboardManager::clipboardFilesChanged);

        // 第一次复制：真实文件 URL 走 hasUrls 分支全链路 → 发射 1 次
        copyFileToClipboard(filePath);
        QCOMPARE(spy.count(), 1);
        const ClipboardFileList first = spy.at(0).at(0).value<ClipboardFileList>();
        QCOMPARE(first.files.size(), 1);
        QCOMPARE(first.files.at(0).fileName, QString("dedup.txt"));
        QCOMPARE(first.files.at(0).fileSize, quint64(5));
        QVERIFY(first.files.at(0).modifyTimeMs > 0);

        // 第二次复制相同内容：去重哈希一致 → 不发射
        copyFileToClipboard(filePath);
        QCOMPARE(spy.count(), 1);
    }

    void testApplyRemoteFilesClearsDeDup() {
        ClipboardManager mgr;
        mgr.setEnabled(true);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = createTempFile(dir, "remote.txt", "hello");
        QVERIFY(!filePath.isEmpty());

        // spy 在 applyRemoteFiles 之前创建，观测完整去重链路
        QSignalSpy spy(&mgr, &ClipboardManager::clipboardFilesChanged);

        // 本地复制 → 发射 1 次并建立去重基线
        copyFileToClipboard(filePath);
        QCOMPARE(spy.count(), 1);

        // 相同内容再次复制 → 去重生效，不发射
        copyFileToClipboard(filePath);
        QCOMPARE(spy.count(), 1);

        // 应用远端文件列表清空去重哈希 → 相同内容本地复制重新发射
        mgr.applyRemoteFiles(ClipboardFileList{});
        copyFileToClipboard(filePath);
        QCOMPARE(spy.count(), 2);
    }

    void testApplyRemoteFilesThenResyncReemits() {
        ClipboardManager mgr;
        mgr.setEnabled(true);

        // 应用远端非空文件列表后，resync 应补发（与文本/图片的认证后补发语义一致）
        ClipboardFileList list;
        list.files.append({"a.txt", 100, 0, false});
        mgr.applyRemoteFiles(list);

        QSignalSpy spy(&mgr, &ClipboardManager::clipboardFilesChanged);
        mgr.resync();
        QCOMPARE(spy.count(), 1);
    }

private:
    // 在临时目录创建真实文件并返回路径（文件必须真实存在，extractFiles 才会收录）
    static QString createTempFile(QTemporaryDir& dir, const QString& name, const QByteArray& content) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly))
            return QString();
        file.write(content);
        file.close();
        return file.fileName();
    }

    // 模拟资源管理器复制：向系统剪贴板写入本地文件 URL 并等待 dataChanged 处理
    static void copyFileToClipboard(const QString& filePath) {
        auto* mimeData = new QMimeData;
        mimeData->setUrls({QUrl::fromLocalFile(filePath)});
        QGuiApplication::clipboard()->setMimeData(mimeData);
        QTest::qWait(50);
    }
};

QTEST_MAIN(ClipboardManagerTest)
#include "test_clipboardmanager.moc"
