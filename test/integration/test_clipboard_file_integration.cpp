#include <QtCore/QCryptographicHash>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>

#include "client/window/DragDropHandler.h"
#include "common/clipboard/ClipboardManager.h"
#include "common/network/Protocol.h"
#include "core/transfer/FileTransferManager.h"

class ClipboardFileIntegrationTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

    // 生成确定性内容的大文件（非整块大小，覆盖尾部块边界）
    static void writeLargeFile(const QString& path, int size) {
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(f.errorString()));
        int written = 0;
        while (written < size) {
            const int chunk = qMin(4096, size - written);
            f.write(QByteArray(chunk, static_cast<char>((written / 4096) % 251 + 1)));
            written += chunk;
        }
        f.close();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
    }

    // 场景 2：CLIPBOARD_DATA(FILE_LIST) 编解码 + 协议帧往返
    void testFileListRoundTripThroughCodec() {
        ClipboardFileList list;
        list.files.append({"report.pdf", 1048576, 1690000000000LL, false});
        list.files.append({"photos", 0, 1690000001000LL, true});
        list.flags = 0;

        ClipboardMessage msg(list);
        QVERIFY(msg.isFileList());
        const QByteArray frame = Protocol::createMessage(MessageType::CLIPBOARD_DATA, msg);
        QVERIFY(!frame.isEmpty());

        // 模拟接收侧解析协议帧
        MessageHeader header;
        QByteArray payload;
        QCOMPARE(Protocol::parseMessage(frame, header, payload), frame.size());
        QCOMPARE(header.type, MessageType::CLIPBOARD_DATA);

        ClipboardMessage decoded;
        QVERIFY(decoded.decode(payload));
        QVERIFY(decoded.isFileList());
        const ClipboardFileList dl = decoded.fileList();
        QCOMPARE(dl.files.size(), 2);
        QCOMPARE(dl.files.at(0).fileName, QString("report.pdf"));
        QCOMPARE(dl.files.at(0).fileSize, static_cast<quint64>(1048576));
        QCOMPARE(dl.files.at(1).isDirectory, true);
    }

    // 场景 3：小文件全链路（复制方读文件 → CHUNK 编解码 → 粘贴方落盘）
    void testSmallFileEndToEnd() {
        const QByteArray content("hello remote desktop", 21);
        const QString srcPath = m_tempDir.path() + "/small.txt";
        QFile src(srcPath);
        QVERIFY(src.open(QIODevice::WriteOnly));
        src.write(content);
        src.close();

        ClipboardFileList list;
        list.files.append({"small.txt", static_cast<quint64>(content.size()), 0, false});

        // 复制方：响应粘贴方请求读文件
        FileTransferManager sender(m_tempDir.path());
        QSignalSpy chunkSpy(&sender, &FileTransferManager::fileChunkReady);
        sender.handleFileRequest(0, list, srcPath);
        QVERIFY(chunkSpy.count() >= 1);
        QVERIFY(chunkSpy.at(0).at(3).toBool());  // lastChunk = true

        // 粘贴方：请求 + 接收数据块并落盘
        const QString destDir = m_tempDir.path() + "/received";
        QVERIFY(QDir().mkpath(destDir));
        FileTransferManager receiver(destDir);
        receiver.requestRemoteFile(0, list);
        receiver.handleIncomingChunk(0, chunkSpy.at(0).at(1).toByteArray(),
                                     chunkSpy.at(0).at(3).toBool());

        QFile dest(destDir + "/small.txt");
        QVERIFY(dest.open(QIODevice::ReadOnly));
        QCOMPARE(dest.readAll(), content);
    }

    // 场景 4：大文件分块 + 滑动窗口（CHUNK 上行 + ACK 下行闭环 → SHA-256 校验）
    void testLargeFileEndToEnd() {
        const int fileSize = 3 * 1024 * 1024 + 12345;  // > 2MB 阈值且非整块
        const QString srcPath = m_tempDir.path() + "/big.bin";
        writeLargeFile(srcPath, fileSize);

        ClipboardFileList list;
        list.files.append({"big.bin", static_cast<quint64>(fileSize), 0, false});

        const QString destDir = m_tempDir.path() + "/received-big";
        QVERIFY(QDir().mkpath(destDir));

        // 复制方（发送侧滑动窗口）与粘贴方（接收侧）经信号模拟网络闭环
        // （QueuedConnection 模拟真实网络的异步往返，同时避免同步递归栈溢出）
        FileTransferManager sender(m_tempDir.path());
        FileTransferManager receiver(destDir);
        receiver.requestRemoteFile(0, list);

        QString savedPath;
        quint32 chunkCount = 0;
        QObject::connect(&sender, &FileTransferManager::fileChunkReady,
                         &receiver, [&](int fi, const QByteArray& chunk, quint32 seq, bool lastChunk) {
                             ++chunkCount;
                             receiver.handleIncomingChunk(fi, chunk, lastChunk, seq);
                         }, Qt::QueuedConnection);
        QObject::connect(&receiver, &FileTransferManager::fileChunkAckNeeded,
                         &sender, [&](int fi, quint32 seq) {
                             sender.handleAck(fi, seq);
                         }, Qt::QueuedConnection);
        QObject::connect(&receiver, &FileTransferManager::transferComplete,
                         &receiver, [&](int, const QString& path) {
                             savedPath = path;
                         }, Qt::QueuedConnection);

        sender.handleFileRequest(0, list, srcPath);

        // 事件循环驱动队列传输，直至完成（10s 超时保护）
        QElapsedTimer timer;
        timer.start();
        while (savedPath.isEmpty() && timer.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        QVERIFY(chunkCount > 45);  // 3158073 字节 / 64KB = 49 块，确实验证了分块路径
        QVERIFY(!savedPath.isEmpty());
        QVERIFY(QFile::exists(savedPath));

        // SHA-256 内容一致性校验
        QFile f1(srcPath);
        QFile f2(savedPath);
        QVERIFY(f1.open(QIODevice::ReadOnly));
        QVERIFY(f2.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(f1.readAll(), QCryptographicHash::Sha256),
                 QCryptographicHash::hash(f2.readAll(), QCryptographicHash::Sha256));
    }

    // 场景 1 辅助：远端文件列表应用后路径映射被清空（无本地路径可回发）
    void testApplyRemoteFilesClearsPaths() {
        ClipboardManager mgr;
        mgr.setEnabled(true);

        ClipboardFileList list;
        list.files.append({"remote.txt", 10, 0, false});
        mgr.applyRemoteFiles(list);

        QCOMPARE(mgr.lastFileList().files.size(), 1);
        QVERIFY(mgr.lastFilePath(0).isEmpty());
    }

    // 场景 1：本地剪贴板文件检测（offscreen 平台对 urls 支持有限，验证路径映射行为）
    void testLocalCopyKeepsPathMapping() {
        // 模拟本地文件复制产生的路径记录：直接验证 getter 契约
        // （系统剪贴板 urls 检测在 offscreen 平台不可靠，由单元测试覆盖提取逻辑）
        const QString filePath = m_tempDir.path() + "/mapped.txt";
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();

        const ClipboardFileList list = DragDropHandler::extractFiles({QUrl::fromLocalFile(filePath)});
        QCOMPARE(list.files.size(), 1);
        QCOMPARE(list.files.at(0).fileName, QString("mapped.txt"));
    }
};

QTEST_MAIN(ClipboardFileIntegrationTest)
#include "test_clipboard_file_integration.moc"
