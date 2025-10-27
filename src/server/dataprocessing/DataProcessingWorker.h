#ifndef DATAPROCESSINGWORKER_H
#define DATAPROCESSINGWORKER_H

#include "../../common/core/threading/Worker.h"
#include "../dataflow/DataFlowStructures.h"
#include "../dataflow/QueueManager.h"
#include "DataProcessing.h"
#include "DataProcessingConfig.h"

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QLoggingCategory>
#include <QtCore/QElapsedTimer>
#include <memory>
#include <atomic>

Q_DECLARE_LOGGING_CATEGORY(lcDataProcessingWorker)
;;

/**
 * @brief 数据处理工作线程类
 *
 * 作为生产者-消费者模式中的数据处理消费者，负责：
 * 1. 从捕获队列中获取原始帧数据
 * 2. 对帧数据进行处理
 * 3. 将处理后的数据放入处理队列
 *
 */
class DataProcessingWorker : public Worker {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit DataProcessingWorker(QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~DataProcessingWorker() override;

    /**
     * @brief 设置处理配置
     * @param config 数据处理配置
     */
    void setProcessingConfig(std::shared_ptr<DataProcessingConfig> config);

    /**
     * @brief 获取处理配置
     * @return 数据处理配置
     */
    std::shared_ptr<DataProcessingConfig> getProcessingConfig() const;

    /**
      * @brief 获取处理统计信息
      * @return 处理统计信息字符串
      */
    QString getProcessingStats() const;

    /**
     * @brief 获取当前处理速率（帧/秒）
     * @return 处理速率
     */
    double getProcessingRate() const;

    /**
     * @brief 获取平均处理延迟（毫秒）
     * @return 平均处理延迟
     */
    double getAverageProcessingLatency() const;

    /**
     * @brief 设置处理超时时间
     * @param timeoutMs 超时时间（毫秒）
     */
    void setProcessingTimeout(int timeoutMs);

    /**
     * @brief 设置最大处理队列大小
     * @param maxSize 最大队列大小
     */
    void setMaxQueueSize(int maxSize);

    /**
     * @brief 设置重试配置
     * @param maxRetries 最大重试次数
     * @param retryDelayMs 重试延迟（毫秒）
     */
    void setRetryConfig(int maxRetries, int retryDelayMs);

    /**
     * @brief 获取详细的性能指标
     * @return 性能指标结构
     */
    struct PerformanceMetrics {
        quint64 processedFrames;
        quint64 droppedFrames;
        quint64 retryCount;
        double averageLatency;
        double processingRate;
        double cpuUsage;
        double memoryUsage;
    };
    PerformanceMetrics getPerformanceMetrics() const;

    /**
     * @brief 设置性能阈值
     * @param maxLatency 最大延迟阈值（毫秒）
     * @param minRate 最小处理速率阈值（帧/秒）
     */
    void setPerformanceThresholds(double maxLatency, double minRate);

    /**
     * @brief 停止工作线程
     *
     * 重写父类的stop方法，在停止时立即禁用自适应模式
     * @param waitForFinish 是否等待完成
     */
    void stop(bool waitForFinish = true) override;

public slots:
    /**
     * @brief 停止数据处理并清空队列
     *
     * 此方法用于在客户端断开连接时立即停止数据处理：
     * 1. 暂停工作线程的处理任务
     * 2. 清空捕获队列和处理队列
     * 3. 重置统计信息
     */
    void stopProcessingAndClearQueues();

    /**
     * @brief 恢复数据处理
     *
     * 当有新客户端连接时调用此方法恢复数据处理
     */
    void resumeProcessing();

protected:
    /**
     * @brief 初始化工作线程
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize() override;

    /**
     * @brief 清理工作线程
     */
    void cleanup() override;

    /**
     * @brief 处理任务 - 自动处理队列中的数据
     *
     * 该方法实现了自动数据处理机制：
     * 1. 使用带超时的阻塞等待获取第一帧数据，避免CPU空转
     * 2. 当队列中有数据时自动触发处理流程
     * 3. 支持批量处理模式，提高处理效率
     * 4. 包含重试机制和错误恢复
     * 5. 实时监控处理性能和资源使用情况
     *
     * 工作流程：
     * - 阻塞等待队列中的数据（100ms超时）
     * - 获取到数据后立即开始批量处理
     * - 对每帧数据进行处理
     * - 将处理结果放入输出队列
     * - 更新统计信息和性能指标
     */
    void processTask() override;

private slots:
    /**
     * @brief 更新统计信息
     */
    void updateStats();

    /**
     * @brief 处理队列警告
     * @param type 队列类型
     * @param message 警告消息
     */
    void onQueueWarning(QueueManager::QueueType type, const QString& message);

    /**
     * @brief 处理队列错误
     * @param type 队列类型
     * @param error 错误消息
     */
    void onQueueError(QueueManager::QueueType type, const QString& error);

signals:
    /**
     * @brief 处理统计更新信号
     * @param processedFrames 已处理帧数
     * @param droppedFrames 丢弃帧数
     * @param averageLatency 平均延迟
     * @param processingRate 处理速率
     */
    void processingStatsUpdated(quint64 processedFrames, quint64 droppedFrames,
        double averageLatency, double processingRate);

    /**
     * @brief 处理错误信号
     * @param error 错误消息
     */
    void processingError(const QString& error);

    /**
     * @brief 处理警告信号
     * @param warning 警告消息
     */
    void processingWarning(const QString& warning);

    /**
     * @brief 性能指标更新信号
     * @param metrics 性能指标
     */
    void performanceMetricsUpdated(const PerformanceMetrics& metrics);

    /**
     * @brief 重试事件信号
     * @param frameId 帧ID
     * @param retryCount 重试次数
     * @param reason 重试原因
     */
    void retryAttempted(quint64 frameId, int retryCount, const QString& reason);

private:
    /**
     * @brief 处理单个帧数据（带重试机制）
     * @param frame 帧数据
     * @return 处理是否成功
     */
    bool processFrameWithRetry(const CapturedFrame& frame);

    /**
     * @brief 处理图像数据
     * @param image 图像数据
     * @param frameId 帧ID
     * @return 处理后的数据
     */
    ProcessedData processImage(const QImage& image, quint64 frameId);

    /**
     * @brief 检查系统资源使用情况
     */
    void checkSystemResources();

    /**
     * @brief 自适应调整处理参数
     */
    void adaptProcessingParameters();

    /**
     * @brief 验证帧数据
     * @param frame 帧数据
     * @return true 数据有效，false 数据无效
     */
    bool validateFrame(const CapturedFrame& frame) const;

    /**
     * @brief 更新处理统计
     * @param processingTime 处理耗时（毫秒）
     * @param success 是否处理成功
     */
    void updateProcessingStats(qint64 processingTime, bool success);

    /**
     * @brief 检查处理性能
     */
    void checkPerformance();

private:
    QueueManager* m_queueManager;                                       ///< 队列管理器
    ThreadSafeQueue<CapturedFrame>* m_captureQueue;                     ///< 捕获队列
    ThreadSafeQueue<ProcessedData>* m_processedQueue;                   ///< 处理队列

    std::shared_ptr<DataProcessingConfig> m_config;                     ///< 处理配置
    std::unique_ptr<DataProcessor> m_dataProcessor;                     ///< 数据处理器

    QTimer* m_statsTimer;                                               ///< 统计更新定时器
    mutable QMutex m_statsMutex;                                        ///< 统计互斥锁

    // 处理统计
    std::atomic<quint64> m_processedFrames;                             ///< 已处理帧数
    std::atomic<quint64> m_droppedFrames;                               ///< 丢弃帧数
    std::atomic<quint64> m_totalProcessingTime;                         ///< 总处理时间（毫秒）
    std::atomic<double> m_averageLatency;                               ///< 平均延迟（毫秒）
    std::atomic<double> m_processingRate;                               ///< 处理速率（帧/秒）

    QElapsedTimer m_performanceTimer;                                   ///< 性能计时器
    qint64 m_lastStatsUpdate;                                           ///< 上次统计更新时间

    // 配置参数
    int m_processingTimeout;                                            ///< 处理超时时间（毫秒）
    int m_maxQueueSize;                                                 ///< 最大队列大小
    int m_statsUpdateInterval;                                          ///< 统计更新间隔（毫秒）

    // 重试配置
    int m_maxRetries;                                                   ///< 最大重试次数
    int m_retryDelayMs;                                                 ///< 重试延迟（毫秒）
    std::atomic<quint64> m_retryCount;                                  ///< 重试计数

    // 性能阈值
    double m_maxLatencyThreshold;                                       ///< 最大延迟阈值（毫秒）
    double m_minRateThreshold;                                          ///< 最小处理速率阈值（帧/秒）

    // 系统资源监控
    std::atomic<double> m_cpuUsage;                                     ///< CPU使用率
    std::atomic<double> m_memoryUsage;                                  ///< 内存使用率
    QTimer* m_resourceMonitorTimer;                                     ///< 资源监控定时器

    // 自适应参数
    bool m_adaptiveMode;                                                ///< 自适应模式开关
    QTimer* m_adaptiveTimer;                                            ///< 自适应调整定时器

    // 性能监控阈值
    static constexpr double MAX_PROCESSING_LATENCY = 100.0;             ///< 最大处理延迟阈值（毫秒）
    static constexpr double MIN_PROCESSING_RATE = 10.0;                 ///< 最小处理速率阈值（帧/秒）
    static constexpr double MAX_CPU_USAGE = 80.0;                       ///< 最大CPU使用率阈值
    static constexpr double MAX_MEMORY_USAGE = 70.0;                    ///< 最大内存使用率阈值

    static constexpr int DEFAULT_PROCESSING_TIMEOUT = 5000;             ///< 默认处理超时时间（毫秒）
    static constexpr int DEFAULT_STATS_INTERVAL = 1000;                 ///< 默认统计更新间隔（毫秒）
};

#endif // DATAPROCESSINGWORKER_H