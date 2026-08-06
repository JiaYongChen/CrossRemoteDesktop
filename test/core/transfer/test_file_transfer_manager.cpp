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

    void testHandleFileRequestLargeFileTriggersInit() {
        // 大文件（>2MB）应触发分块通道，而非直接发 chunk
        ClipboardFileList list;
        list.files.append({"big.bin", 3 * 1024 * 1024, 0, false});

        FileTransferManager mgr(m_tempDir.path());
        QSignalSpy initSpy(&mgr, &FileTransferManager::fileTransferInitRequested);
        QSignalSpy chunkSpy(&mgr, &FileTransferManager::fileChunkReady);

        mgr.handleFileRequest(0, list, m_tempDir.path() + "/big.bin");

        QCOMPARE(initSpy.count(), 1);
        QCOMPARE(initSpy.at(0).at(0).toInt(), 0);
        QCOMPARE(chunkSpy.count(), 0);
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
