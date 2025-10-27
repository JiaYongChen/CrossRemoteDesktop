#ifndef PERFORMANCEOPTIMIZER_H
#define PERFORMANCEOPTIMIZER_H

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QHash>
#include <QtCore/QDateTime>
#include <QtCore/QLoggingCategory>
#include <memory>
#include <chrono>

Q_DECLARE_LOGGING_CATEGORY(performanceOptimizer)

class ThreadManager;
class Worker;

/**
 * @brief 性能优化器类
 *
 * 负责监控和优化系统性能，包括线程调度策略、内存管理和队列优化。
 * 提供自适应性能调整和资源使用监控功能。
 */
class PerformanceOptimizer : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 线程优先级策略
     */
    enum class ThreadPriority {
        Idle = 0,           ///< 空闲优先级
        Low = 1,            ///< 低优先级
        Normal = 2,         ///< 正常优先级
        High = 3,           ///< 高优先级
        Critical = 4        ///< 关键优先级
    };

    /**
     * @brief 内存管理策略
     */
    enum class MemoryStrategy {
        Conservative,       ///< 保守策略（低内存使用）
        Balanced,          ///< 平衡策略（默认）
        Aggressive         ///< 激进策略（高性能）
    };

    /**
     * @brief 队列优化策略
     */
    enum class QueueStrategy {
        FIFO,              ///< 先进先出
        LIFO,              ///< 后进先出
        Priority,          ///< 优先级队列
        Adaptive           ///< 自适应策略
    };

    /**
     * @brief 性能配置结构
     */
    struct PerformanceConfig {
        // 线程调度配置
        ThreadPriority defaultThreadPriority = ThreadPriority::Normal;
        int maxConcurrentThreads = 4;              ///< 最大并发线程数
        int threadPoolSize = 2;                    ///< 线程池大小
        bool enableThreadAffinity = false;         ///< 是否启用线程亲和性

        // 内存管理配置
        MemoryStrategy memoryStrategy = MemoryStrategy::Balanced;
        size_t maxMemoryUsage = 512 * 1024 * 1024; ///< 最大内存使用（字节）
        size_t memoryWarningThreshold = 400 * 1024 * 1024; ///< 内存警告阈值
        bool enableMemoryPooling = true;           ///< 是否启用内存池

        // 队列优化配置
        QueueStrategy queueStrategy = QueueStrategy::Adaptive;
        int defaultQueueSize = 100;                ///< 默认队列大小
        int maxQueueSize = 1000;                   ///< 最大队列大小

        // 监控配置
        int monitoringInterval = 1000;             ///< 监控间隔（毫秒）
        bool enablePerformanceLogging = true;      ///< 是否启用性能日志
        bool enableAutoOptimization = true;        ///< 是否启用自动优化
    };

    /**
     * @brief 性能统计信息
     */
    struct PerformanceStats {
        // CPU使用率
        double cpuUsage = 0.0;                     ///< CPU使用率（百分比）
        double averageCpuUsage = 0.0;              ///< 平均CPU使用率

        // 内存使用
        size_t memoryUsage = 0;                    ///< 当前内存使用（字节）
        size_t peakMemoryUsage = 0;                ///< 峰值内存使用
        double memoryUsagePercent = 0.0;           ///< 内存使用百分比

        // 线程统计
        int activeThreads = 0;                     ///< 活跃线程数
        int totalThreads = 0;                      ///< 总线程数
        double threadEfficiency = 0.0;             ///< 线程效率

        // 队列统计
        int totalQueueSize = 0;                    ///< 总队列大小
        int averageQueueSize = 0;                  ///< 平均队列大小
        double queueThroughput = 0.0;              ///< 队列吞吐量（项/秒）

        // 性能指标
        std::chrono::milliseconds responseTime{ 0 }; ///< 响应时间
        double frameRate = 0.0;                    ///< 帧率（FPS）
        int droppedFrames = 0;                     ///< 丢帧数

        QDateTime lastUpdated;                     ///< 最后更新时间
    };

    /**
     * @brief 获取单例实例
     * @return PerformanceOptimizer单例实例
     */
    static PerformanceOptimizer* instance();

    /**
     * @brief 析构函数
     */
    ~PerformanceOptimizer();

    /**
     * @brief 禁用拷贝构造和赋值操作
     */
    PerformanceOptimizer(const PerformanceOptimizer&) = delete;
    PerformanceOptimizer& operator=(const PerformanceOptimizer&) = delete;

    /**
     * @brief 设置性能配置
     * @param config 性能配置
     */
    void setConfig(const PerformanceConfig& config);

    /**
     * @brief 获取性能配置
     * @return 当前性能配置
     */
    PerformanceConfig getConfig() const;

    /**
     * @brief 启动性能监控
     */
    void startMonitoring();

    /**
     * @brief 停止性能监控
     */
    void stopMonitoring();

    /**
     * @brief 获取性能统计信息
     * @return 当前性能统计
     */
    PerformanceStats getStats() const;

    /**
     * @brief 重置性能统计
     */
    void resetStats();

    /**
     * @brief 优化线程优先级
     * @param threadName 线程名称
     * @param priority 优先级
     */
    void optimizeThreadPriority(const QString& threadName, ThreadPriority priority);

    /**
     * @brief 优化队列大小
     * @param queueName 队列名称
     * @param optimalSize 最优大小
     */
    void optimizeQueueSize(const QString& queueName, int optimalSize);

    /**
     * @brief 触发内存清理
     */
    void triggerMemoryCleanup();

    /**
     * @brief 自动优化性能
     */
    void autoOptimize();

    /**
     * @brief 设置线程亲和性
     * @param threadName 线程名称
     * @param cpuCore CPU核心编号
     */
    void setThreadAffinity(const QString& threadName, int cpuCore);

signals:
    /**
     * @brief 性能统计更新信号
     * @param stats 性能统计信息
     */
    void performanceStatsUpdated(const PerformanceStats& stats);

    /**
     * @brief 性能警告信号
     * @param warning 警告信息
     */
    void performanceWarning(const QString& warning);

    /**
     * @brief 内存使用警告信号
     * @param usage 当前内存使用
     * @param threshold 警告阈值
     */
    void memoryWarning(size_t usage, size_t threshold);

    /**
     * @brief 优化建议信号
     * @param suggestion 优化建议
     */
    void optimizationSuggestion(const QString& suggestion);

private slots:
    /**
     * @brief 监控定时器槽函数
     */
    void onMonitoringTimer();

    /**
     * @brief 自动优化定时器槽函数
     */
    void onAutoOptimizationTimer();

private:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit PerformanceOptimizer(QObject* parent = nullptr);

    /**
     * @brief 更新CPU使用率
     */
    void updateCpuUsage();

    /**
     * @brief 更新内存使用
     */
    void updateMemoryUsage();

    /**
     * @brief 更新线程统计
     */
    void updateThreadStats();

    /**
     * @brief 更新队列统计
     */
    void updateQueueStats();

    /**
     * @brief 检查性能阈值
     */
    void checkPerformanceThresholds();

    /**
     * @brief 生成优化建议
     */
    void generateOptimizationSuggestions();

    /**
     * @brief 应用自动优化
     */
    void applyAutoOptimizations();

private:
    static PerformanceOptimizer* s_instance;    ///< 单例实例

    mutable QMutex m_mutex;                     ///< 互斥锁
    PerformanceConfig m_config;                 ///< 性能配置
    PerformanceStats m_stats;                   ///< 性能统计

    QTimer* m_monitoringTimer;                  ///< 监控定时器
    QTimer* m_autoOptimizationTimer;            ///< 自动优化定时器

    ThreadManager* m_threadManager;             ///< 线程管理器引用

    // 历史数据用于趋势分析
    QHash<QString, QList<double>> m_cpuHistory; ///< CPU使用历史
    QHash<QString, QList<size_t>> m_memoryHistory; ///< 内存使用历史
    QHash<QString, QList<int>> m_queueSizeHistory; ///< 队列大小历史

    bool m_isMonitoring;                        ///< 是否正在监控
    QDateTime m_startTime;                      ///< 监控开始时间
};

#endif // PERFORMANCEOPTIMIZER_H