#pragma once

#include <QtCore/QObject>
#include <QtCore/QDateTime>
#include <QtCore/QByteArray>
#include <QtCore/QRect>
#include <QtGui/QImage>
#include <memory>

/**
 * @brief 捕获帧数据结构
 *
 * 用于在屏幕捕获生产者和数据处理消费者之间传递数据。
 * 包含原始图像数据和相关元信息。
 */
struct CapturedFrame {
    std::shared_ptr<QImage> image;   ///< 捕获的屏幕图像（共享指针以实现零拷贝帧传递）
    QDateTime timestamp;             ///< 捕获时间戳
    quint64 frameId;                 ///< 帧ID，用于追踪和调试
    QSize originalSize;              ///< 原始屏幕尺寸

    // === 区域更新字段 ===
    bool   isFullFrame = true;     ///< 全帧模式（首帧/回退）
    bool   isMoveRect  = false;    ///< 移动区域
    QRect  dirtyRect;              ///< 脏矩形坐标
    QPoint moveSrc;                ///< 移动源坐标
    QPoint moveDst;                ///< 移动目标坐标
    QSize  moveSize;               ///< 移动区域大小

    /**
     * @brief 默认构造函数
     *
     * image is intentionally left as a null shared_ptr; the frame is
     * considered invalid until populated.
     */
    CapturedFrame()
        : image()
        , timestamp(QDateTime::currentDateTime())
        , frameId(0) {
    }

    /**
     * @brief Construct from a QImage by value (copies into a new shared buffer).
     * @param img captured image (implicit-shared copy moved into the shared_ptr)
     * @param id frame id
     */
    explicit CapturedFrame(const QImage& img, quint64 id)
        : image(std::make_shared<QImage>(img))
        , timestamp(QDateTime::currentDateTime())
        , frameId(id)
        , originalSize(img.size()) {
    }

    /**
     * @brief Construct from an rvalue QImage (moves into the shared buffer).
     */
    explicit CapturedFrame(QImage&& img, quint64 id)
        : image(std::make_shared<QImage>(std::move(img)))
        , timestamp(QDateTime::currentDateTime())
        , frameId(id)
        , originalSize(image ? image->size() : QSize()) {
    }

    /**
     * @brief Zero-copy constructor accepting a pre-built shared image (shared ownership).
     *
     * Producers that already allocate a shared_ptr<QImage> (e.g. DXGI capture)
     * can hand the pointer to the frame without any QImage deep-copy.
     * @param img Pre-built shared image pointer. Moved into the frame's member;
     *            if the caller passes an lvalue copy, their shared_ptr remains
     *            valid with the refcount incremented (shared ownership semantics).
     * @param id frame id
     */
    explicit CapturedFrame(std::shared_ptr<QImage> img, quint64 id)
        : image(std::move(img))
        , timestamp(QDateTime::currentDateTime())
        , frameId(id)
        , originalSize(image ? image->size() : QSize()) {
    }

    /**
     * @brief 检查帧数据是否有效
     * @return true 数据有效，false 数据无效
     */
    bool isValid() const {
        return image &&
            !image->isNull() &&
            image->format() != QImage::Format_Invalid &&
            !originalSize.isEmpty() &&
            frameId > 0;
    }

    /**
     * @brief 获取帧数据大小（字节）
     * @return 图像数据大小
     */
    qint64 dataSize() const {
        return image ? image->sizeInBytes() : 0;
    }

    /**
     * @brief 获取帧处理延迟（毫秒）
     * @return 从捕获到现在的延迟
     */
    qint64 getLatency() const {
        return timestamp.msecsTo(QDateTime::currentDateTime());
    }
};

/**
 * @brief 处理后的数据结构
 *
 * 用于在数据处理消费者和数据发送消费者之间传递数据。
 * 包含处理后的数据和传输所需的元信息。
 */
struct ProcessedData {
    QByteArray compressedData;       ///< 处理后的图像数据（JPEG编码）
    QDateTime processedTime;         ///< 处理完成时间戳
    quint64 originalFrameId;         ///< 原始帧ID
    QSize imageSize;                 ///< 图像尺寸
    qint64 originalDataSize;         ///< 原始数据大小
    qint64 compressedDataSize;       ///< 处理后数据大小

    // === 新增字段：脏区域更新元数据（Phase 1 Task 3） ===
    bool isFullFrame = false;        ///< 是否为全帧（客户端需初始化 compositor buffer）
    bool isMoveRect = false;         ///< 是否为移动区域（imageData 前 20B = MoveRect 编码）
    QRect dirtyRect;                 ///< 脏区域坐标（客户端合成位置）
    QPoint moveSrc;                  ///< 移动源坐标（仅 isMoveRect=true）
    QPoint moveDst;                  ///< 移动目标坐标（仅 isMoveRect=true）
    QSize  moveSize;                 ///< 移动区域大小（仅 isMoveRect=true）
    // === 结束：脏区域更新元数据 ===

    bool isScaled;                   ///< 是否进行了缩放
    QSize originalImageSize;         ///< 原始图像尺寸（缩放前）
    quint64 captureTimestamp = 0;    ///< 捕获时间戳 (ms since epoch)，用于端到端延迟测量

    /**
     * @brief 默认构造函数
     */
    ProcessedData()
        : processedTime(QDateTime::currentDateTime())
        , originalFrameId(0)
        , originalDataSize(0)
        , compressedDataSize(0)
        , isScaled(false) {
    }

    /**
     * @brief 构造函数
     * @param data 处理后的数据
     * @param frameId 原始帧ID
     * @param size 图像尺寸
     * @param origSize 原始数据大小
     */
    ProcessedData(const QByteArray& data, quint64 frameId, const QSize& size, qint64 origSize, quint64 captureTs = 0)
        : compressedData(data)
        , processedTime(QDateTime::currentDateTime())
        , originalFrameId(frameId)
        , imageSize(size)
        , originalDataSize(origSize)
        , compressedDataSize(data.size())
        , isScaled(false)
        , originalImageSize(size)
        , captureTimestamp(captureTs) {
    }

    /**
     * @brief 移动构造函数
     */
    ProcessedData(QByteArray&& data, quint64 frameId, const QSize& size, qint64 origSize)
        : compressedData(std::move(data))
        , processedTime(QDateTime::currentDateTime())
        , originalFrameId(frameId)
        , imageSize(size)
        , originalDataSize(origSize)
        , compressedDataSize(compressedData.size())
        , isScaled(false)
        , originalImageSize(size) {
    }

    /**
     * @brief 检查处理数据是否有效
     * @return true 数据有效，false 数据无效
     */
    bool isValid() const {
        return !compressedData.isEmpty() &&
            !imageSize.isEmpty() &&
            originalFrameId > 0 &&
            compressedDataSize > 0;
    }

    /**
     * @brief 获取处理延迟（毫秒）
     * @return 从处理完成到现在的延迟
     */
    qint64 getLatency() const {
        return processedTime.msecsTo(QDateTime::currentDateTime());
    }

    /**
     * @brief 获取数据信息描述
     * @return 数据信息字符串
     */
    QString getDataInfo() const {
        return QString("原始:%1KB, 处理后:%2KB")
            .arg(originalDataSize / 1024)
            .arg(compressedDataSize / 1024);
    }
};

/**
 * @brief 队列统计信息
 *
 * 用于监控队列性能和状态。
 */
struct QueueStats {
    int currentSize;                 ///< 当前队列大小
    int maxSize;                     ///< 最大队列大小
    quint64 totalEnqueued;           ///< 总入队数量
    quint64 totalDequeued;           ///< 总出队数量
    QDateTime lastUpdateTime;        ///< 最后更新时间

    QueueStats()
        : currentSize(0)
        , maxSize(0)
        , totalEnqueued(0)
        , totalDequeued(0)
        , lastUpdateTime(QDateTime::currentDateTime()) {
    }

    double getUsagePercentage() const {
        if ( maxSize <= 0 ) return 0.0;
        return static_cast<double>(currentSize) / maxSize * 100.0;
    }
};

