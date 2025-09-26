#ifndef DIFFERENTIALCOMPRESSION_H
#define DIFFERENTIALCOMPRESSION_H

#include <QtCore/QObject>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QByteArray>
#include <QtCore/QQueue>
#include <QtCore/QSharedPointer>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include "../memory/ZeroCopyData.h"

/**
 * @brief 差分压缩管理器
 * 
 * 提供高效的差分压缩和解压缩功能，支持帧间差分传输。
 * 集成零拷贝优化和性能监控。
 */
class DifferentialCompression : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 压缩结果结构
     */
    struct CompressionResult {
        ZeroCopyByteArrayPtr data;          ///< 压缩后的数据
        bool isDifferential = false;        ///< 是否为差分数据
        qint64 originalSize = 0;            ///< 原始数据大小
        qint64 compressedSize = 0;          ///< 压缩后大小
        double compressionRatio = 0.0;      ///< 压缩比
        qint64 processingTime = 0;          ///< 处理时间（微秒）
        QString errorMessage;               ///< 错误信息
        bool success = false;               ///< 是否成功
    };

    /**
     * @brief 解压缩结果结构
     */
    struct DecompressionResult {
        ZeroCopyByteArrayPtr data;          ///< 解压后的数据
        bool usedFallback = false;          ///< 是否使用了回退策略
        int attemptCount = 0;               ///< 尝试次数
        qint64 processingTime = 0;          ///< 处理时间（微秒）
        QString errorMessage;               ///< 错误信息
        bool success = false;               ///< 是否成功
    };

    /**
     * @brief 性能统计信息
     */
    struct PerformanceStats {
        // 压缩统计
        qint64 totalCompressions = 0;       ///< 总压缩次数
        qint64 differentialCompressions = 0; ///< 差分压缩次数
        qint64 fullCompressions = 0;        ///< 完整压缩次数
        
        // 解压缩统计
        qint64 totalDecompressions = 0;     ///< 总解压次数
        qint64 successfulDecompressions = 0; ///< 成功解压次数
        qint64 fallbackDecompressions = 0;  ///< 回退解压次数
        
        // 性能指标
        double averageCompressionRatio = 0.0; ///< 平均压缩比
        double averageCompressionTime = 0.0;  ///< 平均压缩时间（毫秒）
        double averageDecompressionTime = 0.0; ///< 平均解压时间（毫秒）
        
        // 数据量统计
        qint64 totalOriginalBytes = 0;      ///< 总原始字节数
        qint64 totalCompressedBytes = 0;    ///< 总压缩字节数
        qint64 totalSavedBytes = 0;         ///< 总节省字节数
        
        // 错误统计
        qint64 compressionErrors = 0;       ///< 压缩错误次数
        qint64 decompressionErrors = 0;     ///< 解压错误次数
    };

    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit DifferentialCompression(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~DifferentialCompression();

    /**
     * @brief 压缩数据
     * 
     * 根据是否有前一帧数据，自动选择差分压缩或完整压缩。
     * 
     * @param current 当前数据
     * @param previous 前一帧数据（可选）
     * @return 压缩结果
     */
    CompressionResult compress(const ZeroCopyByteArrayPtr& current, 
                              const ZeroCopyByteArrayPtr& previous = ZeroCopyByteArrayPtr());

    /**
     * @brief 压缩数据（QByteArray版本）
     * @param current 当前数据
     * @param previous 前一帧数据（可选）
     * @return 压缩结果
     */
    CompressionResult compress(const QByteArray& current, const QByteArray& previous = QByteArray());

    /**
     * @brief 解压缩数据
     * 
     * 支持差分解压和完整解压，包含多重回退策略。
     * 
     * @param compressed 压缩数据
     * @param previous 前一帧数据（差分解压时需要）
     * @return 解压结果
     */
    DecompressionResult decompress(const ZeroCopyByteArrayPtr& compressed, 
                                  const ZeroCopyByteArrayPtr& previous = ZeroCopyByteArrayPtr());

    /**
     * @brief 解压缩数据（QByteArray版本）
     * @param compressed 压缩数据
     * @param previous 前一帧数据（差分解压时需要）
     * @return 解压结果
     */
    DecompressionResult decompress(const QByteArray& compressed, const QByteArray& previous = QByteArray());

    /**
     * @brief 设置差分压缩阈值
     * 
     * 当差分数据大小超过原数据的指定比例时，使用完整数据。
     * 
     * @param threshold 阈值（0.0-1.0）
     */
    void setDifferentialThreshold(double threshold);

    /**
     * @brief 获取差分压缩阈值
     * @return 当前阈值
     */
    double differentialThreshold() const;

    /**
     * @brief 设置最大帧缓存数量
     * 
     * 用于存储最近的帧数据，支持多帧差分。
     * 
     * @param maxFrames 最大帧数
     */
    void setMaxFrameCache(int maxFrames);

    /**
     * @brief 获取最大帧缓存数量
     * @return 最大帧数
     */
    int maxFrameCache() const;

    /**
     * @brief 清空帧缓存
     */
    void clearFrameCache();

    /**
     * @brief 获取性能统计
     * @return 性能统计信息
     */
    PerformanceStats getPerformanceStats() const;

    /**
     * @brief 重置性能统计
     */
    void resetPerformanceStats();

    /**
     * @brief 启用/禁用性能监控
     * @param enabled 是否启用
     */
    void setPerformanceMonitoring(bool enabled);

    /**
     * @brief 是否启用了性能监控
     * @return 监控状态
     */
    bool isPerformanceMonitoringEnabled() const;

    /**
     * @brief 获取压缩效率
     * @return 效率百分比（0-100）
     */
    double getCompressionEfficiency() const;

    /**
     * @brief 获取缓存的帧数量
     * @return 帧数量
     */
    int cachedFrameCount() const;

public slots:
    /**
     * @brief 优化缓存
     * 
     * 清理过期的帧缓存，释放内存。
     */
    void optimizeCache();

signals:
    /**
     * @brief 压缩完成信号
     * @param result 压缩结果
     */
    void compressionCompleted(const CompressionResult& result);

    /**
     * @brief 解压完成信号
     * @param result 解压结果
     */
    void decompressionCompleted(const DecompressionResult& result);

    /**
     * @brief 性能警告信号
     * @param message 警告信息
     */
    void performanceWarning(const QString& message);

private:
    /**
     * @brief 帧缓存项
     */
    struct FrameCacheItem {
        ZeroCopyByteArrayPtr data;          ///< 帧数据
        qint64 timestamp;                   ///< 时间戳
        quint32 frameId;                    ///< 帧ID
        qint64 dataSize;                    ///< 数据大小
    };

    /**
     * @brief 执行差分压缩
     * @param current 当前数据
     * @param previous 前一帧数据
     * @return 压缩结果
     */
    CompressionResult performDifferentialCompression(const QByteArray& current, const QByteArray& previous);

    /**
     * @brief 执行完整压缩
     * @param data 数据
     * @return 压缩结果
     */
    CompressionResult performFullCompression(const QByteArray& data);

    /**
     * @brief 执行差分解压
     * @param compressed 压缩数据
     * @param previous 前一帧数据
     * @return 解压结果
     */
    DecompressionResult performDifferentialDecompression(const QByteArray& compressed, const QByteArray& previous);

    /**
     * @brief 执行完整解压
     * @param compressed 压缩数据
     * @return 解压结果
     */
    DecompressionResult performFullDecompression(const QByteArray& compressed);

    /**
     * @brief 添加帧到缓存
     * @param data 帧数据
     * @param frameId 帧ID
     */
    void addFrameToCache(const ZeroCopyByteArrayPtr& data, quint32 frameId);

    /**
     * @brief 从缓存获取帧
     * @param frameId 帧ID
     * @return 帧数据
     */
    ZeroCopyByteArrayPtr getFrameFromCache(quint32 frameId) const;

    /**
     * @brief 更新性能统计
     * @param result 压缩或解压结果
     */
    void updateCompressionStats(const CompressionResult& result);
    void updateDecompressionStats(const DecompressionResult& result);

    /**
     * @brief 检查性能警告
     */
    void checkPerformanceWarnings();

private:
    mutable QMutex m_mutex;                     ///< 互斥锁
    
    // 配置参数
    double m_differentialThreshold;             ///< 差分压缩阈值
    int m_maxFrameCache;                        ///< 最大帧缓存数
    bool m_performanceMonitoring;               ///< 性能监控开关
    
    // 帧缓存
    QQueue<FrameCacheItem> m_frameCache;        ///< 帧缓存队列
    quint32 m_currentFrameId;                   ///< 当前帧ID
    
    // 性能统计
    mutable PerformanceStats m_stats;           ///< 性能统计
    
    // 时间统计
    qint64 m_lastOptimizeTime;                  ///< 上次优化时间
    qint64 m_cacheOptimizeInterval;             ///< 缓存优化间隔
};

#endif // DIFFERENTIALCOMPRESSION_H