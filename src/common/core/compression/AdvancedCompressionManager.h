#ifndef ADVANCEDCOMPRESSIONMANAGER_H
#define ADVANCEDCOMPRESSIONMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QRect>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QElapsedTimer>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <memory>
#include <deque>

#include "Compression.h"

/**
 * @brief 高级压缩管理器类
 * 
 * 提供智能压缩策略选择、屏幕变化检测优化、差分传输算法改进等功能
 * 支持自适应压缩参数调整和性能监控
 */
class AdvancedCompressionManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 压缩策略枚举
     */
    enum CompressionStrategy {
        FastStrategy,           // 快速策略：优先速度
        BalancedStrategy,       // 平衡策略：速度与压缩率平衡
        HighCompressionStrategy, // 高压缩策略：优先压缩率
        AdaptiveStrategy        // 自适应策略：根据内容自动选择
    };

    /**
     * @brief 屏幕变化检测模式
     */
    enum ChangeDetectionMode {
        PixelLevel,             // 像素级检测
        BlockLevel,             // 块级检测
        RegionLevel,            // 区域级检测
        HybridLevel             // 混合检测
    };

    /**
     * @brief 压缩性能统计信息
     */
    struct CompressionStats {
        qint64 totalBytesProcessed;     // 总处理字节数
        qint64 totalBytesCompressed;    // 总压缩后字节数
        double averageCompressionRatio; // 平均压缩率
        qint64 averageCompressionTime;  // 平均压缩时间(毫秒)
        qint64 totalFramesProcessed;    // 总处理帧数
        qint64 differentialFrames;      // 差分帧数
        qint64 fullFrames;              // 完整帧数
        double changeDetectionAccuracy; // 变化检测准确率
    };

    /**
     * @brief 屏幕变化区域信息
     */
    struct ChangeRegion {
        QRect rect;                     // 变化区域矩形
        double changeIntensity;         // 变化强度 (0.0-1.0)
        Compression::Algorithm bestAlgorithm; // 最佳压缩算法
        int recommendedQuality;         // 推荐质量参数
    };

    /**
     * @brief 自适应压缩配置
     */
    struct AdaptiveConfig {
        bool enableAdaptiveStrategy;    // 启用自适应策略
        bool enableChangeDetection;     // 启用变化检测
        bool enablePerformanceMonitoring; // 启用性能监控
        int maxFrameHistory;            // 最大帧历史数量
        double changeThreshold;         // 变化阈值
        int blockSize;                  // 块大小
        qint64 performanceUpdateInterval; // 性能更新间隔(毫秒)
    };

explicit AdvancedCompressionManager(QObject *parent = nullptr);
    ~AdvancedCompressionManager();

    // 配置管理
    void setCompressionStrategy(CompressionStrategy strategy);
    CompressionStrategy compressionStrategy() const;
    
    void setChangeDetectionMode(ChangeDetectionMode mode);
    ChangeDetectionMode changeDetectionMode() const;
    
    void setAdaptiveConfig(const AdaptiveConfig &config);
    AdaptiveConfig adaptiveConfig() const;

    // 核心压缩功能
    QByteArray compressFrame(const QImage &frame, const QString &frameId = QString());
    QByteArray compressFrameDifferential(const QImage &currentFrame, const QImage &previousFrame, const QString &frameId = QString());
    QImage decompressFrame(const QByteArray &compressedData, const QImage &previousFrame = QImage());
    
    // 屏幕变化检测
    QList<ChangeRegion> detectChanges(const QImage &currentFrame, const QImage &previousFrame);
    double calculateFrameSimilarity(const QImage &frame1, const QImage &frame2);
    QByteArray compressChangedRegions(const QImage &currentFrame, const QImage &previousFrame, const QList<ChangeRegion> &changes);
    
    // 智能压缩策略
    Compression::Algorithm selectOptimalAlgorithm(const QByteArray &data, CompressionStrategy strategy = AdaptiveStrategy);
    Compression::Level selectOptimalLevel(const QByteArray &data, Compression::Algorithm algorithm);
    Compression::ImageFormat selectOptimalImageFormat(const QImage &image, CompressionStrategy strategy = AdaptiveStrategy);
    int selectOptimalQuality(const QImage &image, Compression::ImageFormat format, CompressionStrategy strategy = AdaptiveStrategy);
    
    // 性能监控和统计
    CompressionStats getCompressionStats() const;
    void resetStats();
    double getCurrentCompressionRatio() const;
    qint64 getCurrentCompressionTime() const;
    
    // 缓存和历史管理
    void setMaxFrameHistory(int maxFrames);
    int maxFrameHistory() const;
    void clearFrameHistory();
    
    // 实用工具函数
    static QByteArray optimizeDifferentialData(const QByteArray &differentialData);
    static bool isFrameSignificantlyDifferent(const QImage &frame1, const QImage &frame2, double threshold = 0.1);
    static QList<QRect> divideFrameIntoBlocks(const QSize &frameSize, int blockSize);

public slots:
    void updatePerformanceMetrics();
    void optimizeCompressionParameters();

signals:
    void compressionStatsUpdated(const CompressionStats &stats);
    void compressionStrategyChanged(CompressionStrategy newStrategy);
    void performanceThresholdExceeded(const QString &metric, double value);

private slots:
    void onPerformanceTimer();

private:
    // 内部辅助函数
    void initializeDefaults();
    void updateCompressionStats(qint64 originalSize, qint64 compressedSize, qint64 compressionTime, bool isDifferential);
    QByteArray compressWithStrategy(const QByteArray &data, CompressionStrategy strategy);
    QList<ChangeRegion> detectChangesPixelLevel(const QImage &current, const QImage &previous);
    QList<ChangeRegion> detectChangesBlockLevel(const QImage &current, const QImage &previous);
    QList<ChangeRegion> detectChangesRegionLevel(const QImage &current, const QImage &previous);
    QList<ChangeRegion> detectChangesHybridLevel(const QImage &current, const QImage &previous);
    
    double calculateBlockSimilarity(const QImage &image1, const QImage &image2, const QRect &block);
    Compression::Algorithm analyzeDataCharacteristics(const QByteArray &data);
    void adaptStrategyBasedOnPerformance();
    
    // 成员变量
    CompressionStrategy m_strategy;             // 当前压缩策略
    ChangeDetectionMode m_changeDetectionMode;  // 变化检测模式
    AdaptiveConfig m_adaptiveConfig;            // 自适应配置
    
    CompressionStats m_stats;                   // 压缩统计信息
    mutable QMutex m_statsMutex;               // 统计信息互斥锁
    
    std::deque<QImage> m_frameHistory;         // 帧历史缓存
    mutable QMutex m_historyMutex;             // 历史缓存互斥锁
    
    QTimer *m_performanceTimer;                // 性能监控定时器
    QElapsedTimer m_compressionTimer;          // 压缩计时器
    
    // 性能监控变量
    qint64 m_lastCompressionTime;              // 上次压缩时间
    double m_lastCompressionRatio;             // 上次压缩率
    qint64 m_performanceUpdateCounter;         // 性能更新计数器
    
    // 自适应参数
    double m_adaptiveThreshold;                // 自适应阈值
    qint64 m_adaptiveUpdateInterval;           // 自适应更新间隔
    
    static constexpr int DEFAULT_MAX_FRAME_HISTORY = 10;    // 默认最大帧历史数量
    static constexpr double DEFAULT_CHANGE_THRESHOLD = 0.05; // 默认变化阈值
    static constexpr int DEFAULT_BLOCK_SIZE = 32;           // 默认块大小
    static constexpr qint64 DEFAULT_PERFORMANCE_UPDATE_INTERVAL = 1000; // 默认性能更新间隔
};

#endif // ADVANCEDCOMPRESSIONMANAGER_H