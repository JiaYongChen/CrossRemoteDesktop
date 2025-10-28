#ifndef DATAFLOWSTRUCTURES_H
#define DATAFLOWSTRUCTURES_H

#include <QtCore/QObject>
#include <QtCore/QDateTime>
#include <QtCore/QByteArray>
#include <QtGui/QImage>
#include <QtCore/QLoggingCategory>
#include <memory>

Q_DECLARE_LOGGING_CATEGORY(lcDataFlow)
;;

/**
 * @brief 捕获帧数据结构
 *
 * 用于在屏幕捕获生产者和数据处理消费者之间传递数据。
 * 包含原始图像数据和相关元信息。
 */
struct CapturedFrame {
    QImage image;                    ///< 捕获的屏幕图像
    QDateTime timestamp;             ///< 捕获时间戳
    quint64 frameId;                 ///< 帧ID，用于追踪和调试
    QSize originalSize;              ///< 原始屏幕尺寸

    /**
     * @brief 默认构造函数
     */
    CapturedFrame()
        : timestamp(QDateTime::currentDateTime())
        , frameId(0) {
    }

    /**
     * @brief 构造函数
     * @param img 捕获的图像
     * @param id 帧ID
     */
    CapturedFrame(const QImage& img, quint64 id)
        : image(img)
        , timestamp(QDateTime::currentDateTime())
        , frameId(id)
        , originalSize(img.size()) {
    }

    /**
     * @brief 移动构造函数
     */
    CapturedFrame(QImage&& img, quint64 id)
        : image(std::move(img))
        , timestamp(QDateTime::currentDateTime())
        , frameId(id)
        , originalSize(image.size()) {
    }

    /**
     * @brief 检查帧数据是否有效
     * @return true 数据有效，false 数据无效
     */
    bool isValid() const {
        return !image.isNull() &&
            image.format() != QImage::Format_Invalid &&
            !originalSize.isEmpty() &&
            frameId > 0;
    }

    /**
     * @brief 获取帧数据大小（字节）
     * @return 图像数据大小
     */
    qint64 dataSize() const {
        return image.sizeInBytes();
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
    QByteArray compressedData;       ///< 处理后的图像数据（原始像素数据）
    QDateTime processedTime;         ///< 处理完成时间戳
    quint64 originalFrameId;         ///< 原始帧ID
    QSize imageSize;                 ///< 图像尺寸
    qint64 originalDataSize;         ///< 原始数据大小
    qint64 compressedDataSize;       ///< 处理后数据大小

    /**
     * @brief 默认构造函数
     */
    ProcessedData()
        : processedTime(QDateTime::currentDateTime())
        , originalFrameId(0)
        , originalDataSize(0)
        , compressedDataSize(0) {
    }

    /**
     * @brief 构造函数
     * @param data 处理后的数据
     * @param frameId 原始帧ID
     * @param size 图像尺寸
     * @param origSize 原始数据大小
     */
    ProcessedData(const QByteArray& data, quint64 frameId, const QSize& size, qint64 origSize)
        : compressedData(data)
        , processedTime(QDateTime::currentDateTime())
        , originalFrameId(frameId)
        , imageSize(size)
        , originalDataSize(origSize)
        , compressedDataSize(data.size()) {
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
        , compressedDataSize(compressedData.size()) {
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
    quint64 totalDropped;            ///< 总丢弃数量
    double averageLatency;           ///< 平均延迟（毫秒）
    QDateTime lastUpdateTime;        ///< 最后更新时间

    /**
     * @brief 默认构造函数
     */
    QueueStats()
        : currentSize(0)
        , maxSize(0)
        , totalEnqueued(0)
        , totalDequeued(0)
        , totalDropped(0)
        , averageLatency(0.0)
        , lastUpdateTime(QDateTime::currentDateTime()) {
    }

    /**
     * @brief 获取队列使用率
     * @return 使用率百分比（0-100）
     */
    double getUsagePercentage() const {
        if ( maxSize <= 0 ) return 0.0;
        return static_cast<double>(currentSize) / maxSize * 100.0;
    }

    /**
     * @brief 获取吞吐率
     * @return 每秒处理的项目数
     */
    double getThroughput() const {
        auto elapsed = lastUpdateTime.msecsTo(QDateTime::currentDateTime());
        if ( elapsed <= 0 ) return 0.0;
        return static_cast<double>(totalDequeued) / (elapsed / 1000.0);
    }
};

#endif // DATAFLOWSTRUCTURES_H