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
 * - >2MB 大文件：分块传输（FILE_TRANSFER_CHUNK 流），滑动窗口流控（kWindowSize 块未确认）
 * 接收侧负责目标文件创建（同名自动重命名）、分块写入与完成/取消清理。
 */
class FileTransferManager : public QObject {
    Q_OBJECT

public:
    /// 小文件阈值：≤ 该值走剪贴板单块通道，> 该值走大文件分块通道
    static constexpr quint64 kSmallFileThreshold = 2 * 1024 * 1024;
    /// 分块大小（64KB）
    static constexpr quint32 kChunkSize = 64 * 1024;
    /// 滑动窗口：最多 kWindowSize 个未确认块在途
    static constexpr int kWindowSize = 4;

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
     * @param lastChunk 是否最后一块（大文件通道恒为 false）
     * @param seq 块序号（大文件通道有效，用于回发 ACK）
     */
    void handleIncomingChunk(int fileIndex, const QByteArray& chunk, bool lastChunk, quint32 seq = 0);

    /**
     * @brief 发送下一块（大文件分块发送，读满 kChunkSize，EOF 后标记发送完成）
     * @param fileIndex 文件索引
     */
    void sendNextChunk(int fileIndex);

    /**
     * @brief 处理 ACK：推进滑动窗口并补发新块
     * @param fileIndex 文件索引
     * @param ackSeq 粘贴方已确认的最大块序号
     */
    void handleAck(int fileIndex, quint32 ackSeq);

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
     * @brief 文件数据块就绪
     *
     * 小文件通道：seq=0 且 lastChunk=true（单块）；
     * 大文件通道：seq 从 0 递增且 lastChunk=false（无 lastChunk 标志，以字节数判定完成）。
     * @param fileIndex 文件索引
     * @param chunk 数据块
     * @param seq 块序号
     * @param lastChunk 是否最后一块
     */
    void fileChunkReady(int fileIndex, const QByteArray& chunk, quint32 seq, bool lastChunk);

    /**
     * @brief 大文件块已写入，需要回发 ACK（滑动窗口流控）
     * @param fileIndex 文件索引
     * @param seq 已写入块的序号
     */
    void fileChunkAckNeeded(int fileIndex, quint32 seq);

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
        QString sourcePath;           // 发送侧：源文件路径
        QString destPath;             // 接收侧：目标文件路径
        QFile* fileHandle = nullptr;  // 发送侧：读句柄；接收侧：写句柄
        quint64 bytesTransferred = 0; // 发送侧：已发送字节；接收侧：已接收字节
        int currentSeq = 0;           // 发送侧：下一块的 seq
        int lastAckedSeq = -1;        // 发送侧：已确认的最大 seq（-1 = 尚无确认）
        bool isActive = false;
        bool sendComplete = false;    // 发送侧：文件已全部读出
        bool isIncoming = false;      // 区分发送/接收方向，防跨方向碰撞
    };

    QString m_downloadDir;
    QHash<int, TransferContext> m_transfers;
};
