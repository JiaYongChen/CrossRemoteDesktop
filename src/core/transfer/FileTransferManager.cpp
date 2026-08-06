#include "FileTransferManager.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include "common/logging/LoggingCategories.h"

FileTransferManager::FileTransferManager(const QString& downloadDir, QObject* parent)
    : QObject(parent)
    , m_downloadDir(downloadDir) {
}

void FileTransferManager::handleFileRequest(int fileIndex, const ClipboardFileList& fileList,
                                            const QString& sourcePath) {
    if (fileIndex < 0 || fileIndex >= static_cast<int>(fileList.files.size())) {
        qCWarning(lcTransfer) << "handleFileRequest - 文件索引越界:" << fileIndex;
        emit transferError(fileIndex, QStringLiteral("文件索引越界"));
        return;
    }

    const ClipboardFileInfo& info = fileList.files.at(fileIndex);
    if (info.isDirectory) {
        qCWarning(lcTransfer) << "handleFileRequest - 暂不支持传输目录:" << info.fileName;
        emit transferError(fileIndex, QStringLiteral("暂不支持传输目录: %1").arg(info.fileName));
        return;
    }

    if (info.fileSize <= kSmallFileThreshold) {
        // 小文件：整读 + 单块发送（lastChunk = true）
        QFile file(sourcePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(lcTransfer) << "handleFileRequest - 无法打开源文件:" << sourcePath;
            emit transferError(fileIndex, QStringLiteral("无法打开源文件: %1").arg(sourcePath));
            return;
        }
        const QByteArray data = file.readAll();
        file.close();

        if (static_cast<quint64>(data.size()) != info.fileSize) {
            qCWarning(lcTransfer) << "handleFileRequest - 源文件大小与元数据不符:"
                                  << info.fileName << "expected" << info.fileSize << "actual" << data.size();
            emit transferError(fileIndex, QStringLiteral("源文件大小与元数据不符: %1").arg(info.fileName));
            return;
        }

        qCDebug(lcTransfer) << "小文件读取完成，单块发送:" << info.fileName << data.size();
        emit fileChunkReady(fileIndex, data, 0, true);
    } else {
        // 大文件：通知上层走分块通道（FileTransferInit）
        qCDebug(lcTransfer) << "大文件触发分块传输:" << info.fileName << info.fileSize;
        emit fileTransferInitRequested(fileIndex, info);
    }
}

void FileTransferManager::requestRemoteFile(int fileIndex, const ClipboardFileList& fileList) {
    if (fileIndex < 0 || fileIndex >= static_cast<int>(fileList.files.size())) {
        qCWarning(lcTransfer) << "requestRemoteFile - 文件索引越界:" << fileIndex;
        emit transferError(fileIndex, QStringLiteral("文件索引越界"));
        return;
    }

    const ClipboardFileInfo& info = fileList.files.at(fileIndex);
    if (m_transfers.contains(fileIndex)) {
        qCWarning(lcTransfer) << "requestRemoteFile - 传输已在进行中:" << fileIndex;
        emit transferError(fileIndex, QStringLiteral("该文件传输已在进行中"));
        return;
    }

    const QString destPath = resolveCollision(m_downloadDir, info.fileName);
    QFile* file = new QFile(destPath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        qCWarning(lcTransfer) << "requestRemoteFile - 无法创建目标文件:" << destPath;
        emit transferError(fileIndex, QStringLiteral("无法创建目标文件: %1").arg(destPath));
        file->deleteLater();
        return;
    }

    TransferContext ctx;
    ctx.fileInfo = info;
    ctx.destPath = destPath;
    ctx.fileHandle = file;
    ctx.isActive = true;
    m_transfers.insert(fileIndex, ctx);

    qCDebug(lcTransfer) << "开始接收远端文件:" << info.fileName << "→" << destPath;
}

void FileTransferManager::handleIncomingChunk(int fileIndex, const QByteArray& chunk, bool lastChunk) {
    auto it = m_transfers.find(fileIndex);
    if (it == m_transfers.end()) {
        qCWarning(lcTransfer) << "handleIncomingChunk - 未找到进行中的传输:" << fileIndex;
        emit transferError(fileIndex, QStringLiteral("未找到进行中的传输"));
        return;
    }

    TransferContext& ctx = it.value();
    if (!ctx.fileHandle || !ctx.fileHandle->isOpen()) {
        qCWarning(lcTransfer) << "handleIncomingChunk - 目标文件未打开:" << ctx.destPath;
        emit transferError(fileIndex, QStringLiteral("目标文件未打开: %1").arg(ctx.destPath));
        return;
    }

    const qint64 written = ctx.fileHandle->write(chunk);
    if (written != chunk.size()) {
        qCCritical(lcTransfer) << "handleIncomingChunk - 写入失败:" << ctx.destPath;
        ctx.fileHandle->close();
        ctx.fileHandle->deleteLater();
        ctx.fileHandle = nullptr;
        QFile::remove(ctx.destPath);
        m_transfers.erase(it);
        emit transferError(fileIndex, QStringLiteral("写入失败: %1").arg(ctx.destPath));
        return;
    }

    ctx.bytesTransferred += static_cast<quint64>(chunk.size());
    emit transferProgress(fileIndex, ctx.bytesTransferred, ctx.fileInfo.fileSize);

    // 大文件通道（FILE_TRANSFER_CHUNK）无 lastChunk 标志：以接收字节数达到元数据大小判定完成
    const bool transferDone = lastChunk || ctx.bytesTransferred >= ctx.fileInfo.fileSize;
    if (transferDone) {
        ctx.fileHandle->close();
        ctx.fileHandle->deleteLater();
        ctx.fileHandle = nullptr;

        const QString savedPath = ctx.destPath;
        const bool sizeOk = (ctx.bytesTransferred == ctx.fileInfo.fileSize);
        m_transfers.erase(it);

        if (sizeOk) {
            qCInfo(lcTransfer) << "文件接收完成:" << savedPath;
            emit transferComplete(fileIndex, savedPath);
        } else {
            qCWarning(lcTransfer) << "接收大小与元数据不符，删除:" << savedPath;
            QFile::remove(savedPath);
            emit transferError(fileIndex, QStringLiteral("接收大小与元数据不符: %1").arg(savedPath));
        }
    }
}

void FileTransferManager::cancelTransfer(int fileIndex) {
    auto it = m_transfers.find(fileIndex);
    if (it == m_transfers.end()) {
        qCDebug(lcTransfer) << "cancelTransfer - 无进行中的传输:" << fileIndex;
        return;
    }

    TransferContext& ctx = it.value();
    if (ctx.fileHandle) {
        ctx.fileHandle->close();
        ctx.fileHandle->deleteLater();
        ctx.fileHandle = nullptr;
    }
    if (!ctx.destPath.isEmpty()) {
        QFile::remove(ctx.destPath);
    }
    m_transfers.erase(it);

    qCInfo(lcTransfer) << "传输已取消并清理:" << fileIndex;
    emit transferError(fileIndex, QStringLiteral("传输已取消"));
}

void FileTransferManager::cancelAllTransfers() {
    const QList<int> keys = m_transfers.keys();
    for (int key : keys) {
        cancelTransfer(key);
    }
}

void FileTransferManager::setDownloadDirectory(const QString& path) {
    m_downloadDir = path;
}

QString FileTransferManager::downloadDirectory() const {
    return m_downloadDir;
}

QString FileTransferManager::resolveCollision(const QString& dir, const QString& fileName) const {
    const QFileInfo fi(fileName);
    const QString baseName = fi.completeBaseName();
    const QString suffix = fi.suffix();

    const auto join = [&dir](const QString& name) {
        return dir + QDir::separator() + name;
    };

    QString path = join(fileName);
    if (!QFile::exists(path)) return path;

    for (int i = 1; i <= 999; ++i) {
        const QString numberedName = suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(baseName).arg(i)
            : QStringLiteral("%1 (%2).%3").arg(baseName).arg(i).arg(suffix);
        path = join(numberedName);
        if (!QFile::exists(path)) return path;
    }
    // 兜底：连 999 个编号都冲突时返回原始名（由调用方处理失败）
    return join(fileName);
}
