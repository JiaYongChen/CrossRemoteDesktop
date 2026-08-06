#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>

#include "core/transfer/FileTransferManager.h"
#include "common/network/Protocol.h"

class FileTransferManagerTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

private slots:
    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
    }

    void testHandleFileRequestSmallFile() {
        // 在临时目录创建小文件 (≤2MB)
        QString srcPath = m_tempDir.path() + "/small.txt";
        QFile srcFile(srcPath);
        QVERIFY(srcFile.open(QIODevice::WriteOnly));
        srcFile.write(QByteArray(1024, 'A'));
        srcFile.close();

        ClipboardFileList list;
        list.files.append({"small.txt", 1024, 0, false});

        FileTransferManager mgr(m_tempDir.path());
        QSignalSpy spy(&mgr, &FileTransferManager::fileChunkReady);
        mgr.handleFileRequest(0, list, srcPath);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0); // fileIndex
        QCOMPARE(spy.at(0).at(1).toByteArray().size(), 1024); // chunk 内容
        QVERIFY(spy.at(0).at(3).toBool());    // lastChunk = true
    }

    void testHandleFileRequestLargeFileChunkedSend() {
        // 大文件（>2MB）→ 分块发送状态机：初始填充窗口，ACK 逐块推进，最后一块 ACK 收尾
        const qint64 fileSize = 3 * 1024 * 1024;  // 3MB = 48 块 × 64KB
        QString srcPath = m_tempDir.path() + "/big.bin";
        QFile srcFile(srcPath);
        QVERIFY(srcFile.open(QIODevice::WriteOnly));
        srcFile.write(QByteArray(fileSize, 'B'));
        srcFile.close();

        ClipboardFileList list;
        list.files.append({"big.bin", static_cast<quint64>(fileSize), 0, false});

        FileTransferManager mgr(m_tempDir.path());
        QSignalSpy chunkSpy(&mgr, &FileTransferManager::fileChunkReady);
        QSignalSpy doneSpy(&mgr, &FileTransferManager::transferComplete);

        mgr.handleFileRequest(0, list, srcPath);

        // 初始窗口：kWindowSize 块，每块 64KB，seq 从 0 递增，lastChunk=false
        QCOMPARE(chunkSpy.count(), FileTransferManager::kWindowSize);
        QCOMPARE(chunkSpy.at(0).at(1).toByteArray().size(),
                 static_cast<int>(FileTransferManager::kChunkSize));
        QCOMPARE(chunkSpy.at(0).at(2).toUInt(), 0u);
        QCOMPARE(chunkSpy.at(3).at(2).toUInt(), 3u);
        QVERIFY(!chunkSpy.at(3).at(3).toBool());

        // ACK 推进：每确认一块补发一块（共 48 块）
        mgr.handleAck(0, 0);
        QCOMPARE(chunkSpy.count(), FileTransferManager::kWindowSize + 1);
        QCOMPARE(chunkSpy.at(4).at(2).toUInt(), 4u);

        for (quint32 seq = 1; seq < 44; ++seq) {
            mgr.handleAck(0, seq);
        }
        QCOMPARE(chunkSpy.count(), 48);  // 全部 48 块已发出
        QCOMPARE(chunkSpy.at(47).at(2).toUInt(), 47u);
        QVERIFY(!chunkSpy.at(47).at(3).toBool());
        QCOMPARE(doneSpy.count(), 0);    // 窗口尚有未确认块

        // 最终 ACK 清空窗口 → 传输完成
        mgr.handleAck(0, 47);
        QCOMPARE(doneSpy.count(), 1);
        QCOMPARE(doneSpy.at(0).at(0).toInt(), 0);
    }

    void testHandleFileRequestLargeFileSizeMismatch() {
        // 大文件元数据与实际大小不符：打开后校验失败，不发任何块
        QString srcPath = m_tempDir.path() + "/big_mismatch.bin";
        QFile f(srcPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1024, 'A'));
        f.close();

        ClipboardFileList list;
        list.files.append({"big_mismatch.bin", 3 * 1024 * 1024, 0, false});

        FileTransferManager mgr(m_tempDir.path());
        QSignalSpy errSpy(&mgr, &FileTransferManager::transferError);
        QSignalSpy chunkSpy(&mgr, &FileTransferManager::fileChunkReady);

        mgr.handleFileRequest(0, list, srcPath);

        QCOMPARE(errSpy.count(), 1);
        QCOMPARE(chunkSpy.count(), 0);
    }

    void testHandleFileRequestRejectsDuplicate() {
        // 大文件传输进行中，重复请求应报错而非覆盖上下文
        const qint64 fileSize = 3 * 1024 * 1024;
        QString srcPath = m_tempDir.path() + "/dup.bin";
        QFile f(srcPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(fileSize, 'C'));
        f.close();

        ClipboardFileList list;
        list.files.append({"dup.bin", static_cast<quint64>(fileSize), 0, false});

        FileTransferManager mgr(m_tempDir.path());
        QSignalSpy errSpy(&mgr, &FileTransferManager::transferError);

        mgr.handleFileRequest(0, list, srcPath);
        mgr.handleFileRequest(0, list, srcPath);  // 传输进行中

        QCOMPARE(errSpy.count(), 1);
    }

    void testIncomingChunkEmitsAckForLargeFile() {
        // 接收大文件块：每块回发 ACK（含最后一块），全部到齐后完成
        ClipboardFileList list;
        list.files.append({"ack_test.bin", 2 * static_cast<quint64>(FileTransferManager::kChunkSize), 0, false});

        FileTransferManager mgr(m_tempDir.path());
        QSignalSpy ackSpy(&mgr, &FileTransferManager::fileChunkAckNeeded);
        QSignalSpy completeSpy(&mgr, &FileTransferManager::transferComplete);

        mgr.requestRemoteFile(0, list);
        mgr.handleIncomingChunk(0, QByteArray(FileTransferManager::kChunkSize, 'A'), false, 5);

        QCOMPARE(ackSpy.count(), 1);
        QCOMPARE(ackSpy.at(0).at(0).toInt(), 0);    // fileIndex
        QCOMPARE(ackSpy.at(0).at(1).toUInt(), 5u);  // seq
        QCOMPARE(completeSpy.count(), 0);           // 只收到一半

        // 最后一块：先发 ACK 再完成
        mgr.handleIncomingChunk(0, QByteArray(FileTransferManager::kChunkSize, 'B'), false, 6);
        QCOMPARE(ackSpy.count(), 2);
        QCOMPARE(ackSpy.at(1).at(1).toUInt(), 6u);
        QCOMPARE(completeSpy.count(), 1);
    }

    void testIncomingChunkNoAckForSmallFileChannel() {
        // 小文件通道（lastChunk=true）不应触发 ACK
        ClipboardFileList list;
        list.files.append({"small_ack.bin", 100, 0, false});

        FileTransferManager mgr(m_tempDir.path());
        QSignalSpy ackSpy(&mgr, &FileTransferManager::fileChunkAckNeeded);

        mgr.requestRemoteFile(0, list);
        mgr.handleIncomingChunk(0, QByteArray(100, 'A'), true);

        QCOMPARE(ackSpy.count(), 0);
    }

    void testHandleFileRequestSizeMismatch() {
        QString srcPath = m_tempDir.path() + "/mismatch.txt";
        QFile f(srcPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(10, 'A'));
        f.close();

        ClipboardFileList list;
        list.files.append({"mismatch.txt", 999, 0, false}); // 元数据与实际不符

        FileTransferManager mgr(m_tempDir.path());
        QSignalSpy spy(&mgr, &FileTransferManager::transferError);
        mgr.handleFileRequest(0, list, srcPath);

        QCOMPARE(spy.count(), 1);
    }

    void testRequestRemoteFileWritesCorrectly() {
        ClipboardFileList list;
        list.files.append({"received.dat", 512, 0, false});

        FileTransferManager mgr(m_tempDir.path());
        mgr.requestRemoteFile(0, list);

        // 模拟接收到数据块
        mgr.handleIncomingChunk(0, QByteArray(256, 'B'), false); // not last
        mgr.handleIncomingChunk(0, QByteArray(256, 'C'), true);  // last

        // 验证文件写入正确
        QString destPath = m_tempDir.path() + "/received.dat";
        QVERIFY(QFile::exists(destPath));
        QFile f(destPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.size(), static_cast<qint64>(512));
        f.close();
    }

    void testSameNameFileRenamesCorrectly() {
        // 创建同名文件
        QString existing = m_tempDir.path() + "/duplicate.dat";
        QFile ef(existing);
        QVERIFY(ef.open(QIODevice::WriteOnly));
        ef.write(QByteArray(100, 'X'));
        ef.close();

        ClipboardFileList list;
        list.files.append({"duplicate.dat", 200, 0, false});

        FileTransferManager mgr(m_tempDir.path());
        QSignalSpy spy(&mgr, &FileTransferManager::transferComplete);
        mgr.requestRemoteFile(0, list);
        mgr.handleIncomingChunk(0, QByteArray(200, 'Y'), true);

        QVERIFY(spy.count() >= 1);
        // 验证新文件被重命名
        QString renamedPath = spy.at(0).at(1).toString();
        QVERIFY(renamedPath.contains("duplicate"));
        QVERIFY(renamedPath != existing);
    }

    void testCancelTransferCleansUp() {
        ClipboardFileList list;
        list.files.append({"canceled.dat", 100000, 0, false});

        FileTransferManager mgr(m_tempDir.path());
        mgr.requestRemoteFile(0, list);
        mgr.handleIncomingChunk(0, QByteArray(1024, 'D'), false);
        mgr.cancelTransfer(0);

        // 验证半成品文件已清理
        QVERIFY(!QFile::exists(m_tempDir.path() + "/canceled.dat"));
    }
};

QTEST_MAIN(FileTransferManagerTest)
#include "test_file_transfer_manager.moc"
