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

    if (m_transfers.contains(fileIndex)) {
        qCWarning(lcTransfer) << "handleFileRequest - 传输已在进行中:" << fileIndex;
        emit transferError(fileIndex, QStringLiteral("该文件传输已在进行中"));
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
        emit transferComplete(fileIndex, sourcePath);
    } else {
        // 大文件：打开源文件进入分块发送状态机，先填充初始滑动窗口
        auto* file = new QFile(sourcePath, this);
        if (!file->open(QIODevice::ReadOnly)) {
            qCWarning(lcTransfer) << "handleFileRequest - 无法打开源文件:" << sourcePath;
            emit transferError(fileIndex, QStringLiteral("无法打开源文件: %1").arg(sourcePath));
            file->deleteLater();
            return;
        }
        if (file->size() != static_cast<qint64>(info.fileSize)) {
            qCWarning(lcTransfer) << "handleFileRequest - 源文件大小与元数据不符:"
                                  << info.fileName << "expected" << info.fileSize << "actual" << file->size();
            file->close();
            file->deleteLater();
            emit transferError(fileIndex, QStringLiteral("源文件大小与元数据不符: %1").arg(info.fileName));
            return;
        }

        TransferContext ctx;
        ctx.fileInfo = info;
        ctx.sourcePath = sourcePath;
        ctx.fileHandle = file;
        ctx.isActive = true;
        m_transfers.insert(fileIndex, ctx);

        qCDebug(lcTransfer) << "大文件开始分块发送:" << info.fileName << info.fileSize;
        for (int i = 0; i < kWindowSize; ++i) {
            sendNextChunk(fileIndex);
        }
    }
}

void FileTransferManager::sendNextChunk(int fileIndex) {
    auto it = m_transfers.find(fileIndex);
    if (it == m_transfers.end() || !it.value().isActive || !it.value().fileHandle) return;

    TransferContext& ctx = it.value();
    if (ctx.sendComplete) return;

    QFile* file = ctx.fileHandle;
    const QByteArray data = file->read(static_cast<qint64>(kChunkSize));
    if (data.isEmpty() && !file->atEnd()) {
        // 非 EOF 空读 = 读取失败
        qCCritical(lcTransfer) << "sendNextChunk - 读取源文件失败:" << ctx.sourcePath;
        const QString srcPath = ctx.sourcePath;
        file->close();
        file->deleteLater();
        ctx.fileHandle = nullptr;
        m_transfers.erase(it);
        emit transferError(fileIndex, QStringLiteral("读取源文件失败: %1").arg(srcPath));
        return;
    }

    const quint32 seq = static_cast<quint32>(ctx.currentSeq++);
    ctx.bytesTransferred += static_cast<quint64>(data.size());
    emit fileChunkReady(fileIndex, data, seq, false);  // 大文件通道无 lastChunk 标志
    emit transferProgress(fileIndex, ctx.bytesTransferred, ctx.fileInfo.fileSize);

    if (file->atEnd()) {
        // 文件已全部读出：保留上下文，待最终 ACK 清空窗口后由 handleAck 收尾
        ctx.sendComplete = true;
        file->close();
        file->deleteLater();
        ctx.fileHandle = nullptr;
    }
}

void FileTransferManager::handleAck(int fileIndex, quint32 ackSeq) {
    auto it = m_transfers.find(fileIndex);
    if (it == m_transfers.end()) return;

    TransferContext& ctx = it.value();
    if (!ctx.isActive || static_cast<int>(ackSeq) <= ctx.lastAckedSeq) return;  // 过期 ACK 忽略

    ctx.lastAckedSeq = static_cast<int>(ackSeq);

    // 窗口推进：补发至窗口满或文件读完
    if (!ctx.sendComplete) {
        const int inflight = ctx.currentSeq - (ctx.lastAckedSeq + 1);
        for (int i = inflight; i < kWindowSize; ++i) {
            sendNextChunk(fileIndex);
            // sendNextChunk 同步发射信号可递归调用本方法并清除上下文 → 迭代器失效，必须重新查找
            it = m_transfers.find(fileIndex);
            if (it == m_transfers.end()) return;
            if (it.value().sendComplete) break;
        }
    }

    // 重新查找：递归路径可能已清除上下文
    it = m_transfers.find(fileIndex);
    if (it == m_transfers.end()) return;

    // 发送完成且所有块已确认 → 传输结束
    if (it.value().sendComplete && it.value().currentSeq - (it.value().lastAckedSeq + 1) <= 0) {
        const QString sourcePath = it.value().sourcePath;
        m_transfers.erase(it);
        qCInfo(lcTransfer) << "大文件分块发送完成:" << sourcePath;
        emit transferComplete(fileIndex, sourcePath);
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

void FileTransferManager::handleIncomingChunk(int fileIndex, const QByteArray& chunk, bool lastChunk,
                                              quint32 seq) {
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
        const QString destPath = ctx.destPath;
        ctx.fileHandle->close();
        ctx.fileHandle->deleteLater();
        ctx.fileHandle = nullptr;
        QFile::remove(destPath);
        m_transfers.erase(it);
        emit transferError(fileIndex, QStringLiteral("写入失败: %1").arg(destPath));
        return;
    }

    ctx.bytesTransferred += static_cast<quint64>(chunk.size());
    emit transferProgress(fileIndex, ctx.bytesTransferred, ctx.fileInfo.fileSize);

    // 大文件通道无 lastChunk 标志：以接收字节数达到元数据大小判定完成
    const bool transferDone = lastChunk || ctx.bytesTransferred >= ctx.fileInfo.fileSize;
    if (transferDone) {
        ctx.fileHandle->close();
        ctx.fileHandle->deleteLater();
        ctx.fileHandle = nullptr;

        const QString savedPath = ctx.destPath;
        const bool sizeOk = (ctx.bytesTransferred == ctx.fileInfo.fileSize);
        m_transfers.erase(it);

        if (sizeOk) {
            // 最后一块 ACK 在 size 确认后发出，避免通知发送方完成却随后报错
            if (!lastChunk)
                emit fileChunkAckNeeded(fileIndex, seq);
            qCInfo(lcTransfer) << "文件接收完成:" << savedPath;
            emit transferComplete(fileIndex, savedPath);
        } else {
            qCWarning(lcTransfer) << "接收大小与元数据不符，删除:" << savedPath;
            QFile::remove(savedPath);
            emit transferError(fileIndex, QStringLiteral("接收大小与元数据不符: %1").arg(savedPath));
        }
    } else {
        // 常规块：立即回发 ACK 推进发送方滑动窗口
        emit fileChunkAckNeeded(fileIndex, seq);
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
