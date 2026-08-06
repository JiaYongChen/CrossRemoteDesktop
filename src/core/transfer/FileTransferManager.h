#pragma once

#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>

#include "common/network/Protocol.h"

/**
 * @brief 文件传输管理器
 *
 * 负责文件数据的读取/写入和传输通道选择：
 * - ≤2MB 小文件：整读后通过剪贴板文件块通道（CLIPBOARD_FILE_CHUNK）单块发送
 * - >2MB 大文件：通知上层发起分块传输（FILE_TRANSFER_INIT/CHUNK/ACK）
 * 接收侧负责目标文件创建（同名自动重命名）、分块写入与完成/取消清理。
 */
class FileTransferManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param downloadDir 接收文件保存目录
     * @param parent 父对象
     */
    explicit FileTransferManager(const QString& downloadDir, QObject* parent = nullptr);

    /**
     * @brief 处理远端文件数据请求（粘贴方请求 → 本机读取并回发）
     * @param fileIndex 文件在 FILE_LIST 中的索引
     * @param fileList 文件元数据列表
     * @param sourcePath 源文件完整路径
     */
    void handleFileRequest(int fileIndex, const ClipboardFileList& fileList, const QString& sourcePath);

    /**
     * @brief 请求接收远端文件（创建目标文件，记录传输上下文）
     * @param fileIndex 文件在 FILE_LIST 中的索引
     * @param fileList 文件元数据列表
     */
    void requestRemoteFile(int fileIndex, const ClipboardFileList& fileList);

    /**
     * @brief 处理接收到的文件数据块
     * @param fileIndex 文件索引
     * @param chunk 数据块
     * @param lastChunk 是否最后一块
     */
    void handleIncomingChunk(int fileIndex, const QByteArray& chunk, bool lastChunk);

    /**
     * @brief 取消单个传输（关闭并删除半成品文件）
     * @param fileIndex 文件索引
     */
    void cancelTransfer(int fileIndex);

    /**
     * @brief 取消全部进行中的传输
     */
    void cancelAllTransfers();

    /**
     * @brief 设置接收目录
     * @param path 目录路径
     */
    void setDownloadDirectory(const QString& path);

    /**
     * @brief 获取接收目录
     * @return 目录路径
     */
    QString downloadDirectory() const;

signals:
    /**
     * @brief 文件数据块就绪（小文件通道）
     * @param fileIndex 文件索引
     * @param chunk 数据块
     * @param seq 块序号
     * @param lastChunk 是否最后一块
     */
    void fileChunkReady(int fileIndex, const QByteArray& chunk, quint32 seq, bool lastChunk);

    /**
     * @brief 传输进度
     * @param fileIndex 文件索引
     * @param bytesSent 已传输字节数
     * @param total 总字节数
     */
    void transferProgress(int fileIndex, quint64 bytesSent, quint64 total);

    /**
     * @brief 传输完成
     * @param fileIndex 文件索引
     * @param savedPath 保存路径
     */
    void transferComplete(int fileIndex, const QString& savedPath);

    /**
     * @brief 传输错误（含取消）
     * @param fileIndex 文件索引
     * @param errorMessage 错误信息
     */
    void transferError(int fileIndex, const QString& errorMessage);

    /**
     * @brief 大文件传输发起请求（>2MB 走分块通道）
     * @param fileIndex 文件索引
     * @param info 文件元数据
     */
    void fileTransferInitRequested(int fileIndex, const ClipboardFileInfo& info);

private:
    /**
     * @brief 解析同名冲突：存在则追加递增编号 "文件名 (N).ext"
     * @param dir 目标目录
     * @param fileName 文件名
     * @return 不冲突的完整路径
     */
    QString resolveCollision(const QString& dir, const QString& fileName) const;

    struct TransferContext {
        ClipboardFileInfo fileInfo;
        QString sourcePath;
        QString destPath;
        QFile* fileHandle = nullptr;
        quint64 bytesTransferred = 0;
        int currentSeq = 0;
        bool isActive = false;
    };

    QString m_downloadDir;
    QHash<int, TransferContext> m_transfers;
    static constexpr quint32 kChunkSize = 64 * 1024;
    static constexpr quint32 kSmallFileThreshold = 2 * 1024 * 1024;
};
