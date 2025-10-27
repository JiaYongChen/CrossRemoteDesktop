#ifndef QUEUEMANAGER_H
#define QUEUEMANAGER_H

#include "DataFlowStructures.h"
#include "../../common/core/threading/ThreadSafeQueue.h"
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QLoggingCategory>
#include <memory>

Q_DECLARE_LOGGING_CATEGORY(lcQueueManager)
;;

/**
 * @brief 队列管理器类
 *
 * 管理生产者-消费者模式中的两个主要队列：
 * 1. CaptureQueue: 屏幕捕获 -> 数据处理
 * 2. ProcessedQueue: 数据处理 -> 数据发送
 *
 * 提供队列统计、监控和配置功能。
 */
class QueueManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 队列类型枚举
     */
    enum QueueType {
        CaptureQueue,    ///< 捕获队列
        ProcessedQueue   ///< 处理队列
    };

    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit QueueManager(QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~QueueManager() override;

    /**
     * @brief 获取单例实例
     * @return 队列管理器单例
     */
    static QueueManager* instance();

    /**
     * @brief 初始化队列管理器
     * @param captureQueueSize 捕获队列最大大小（0表示无限制）
     * @param processedQueueSize 处理队列最大大小（0表示无限制）
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize(int captureQueueSize = 10, int processedQueueSize = 5);

    /**
     * @brief 清理队列管理器
     */
    void cleanup();

    /**
     * @brief 获取捕获队列
     * @return 捕获队列指针
     */
    ThreadSafeQueue<CapturedFrame>* getCaptureQueue();

    /**
     * @brief 获取处理队列
     * @return 处理队列指针
     */
    ThreadSafeQueue<ProcessedData>* getProcessedQueue();

    /**
     * @brief 获取队列统计信息
     * @param type 队列类型
     * @return 队列统计信息
     */
    QueueStats getQueueStats(QueueType type) const;

    /**
     * @brief 设置队列最大大小
     * @param type 队列类型
     * @param maxSize 最大大小（0表示无限制）
     */
    void setQueueMaxSize(QueueType type, int maxSize);

    /**
     * @brief 清空指定队列
     * @param type 队列类型
     */
    void clearQueue(QueueType type);

    /**
     * @brief 停止所有队列
     */
    void stopAllQueues();

    /**
     * @brief 重启所有队列
     */
    void restartAllQueues();

    /**
     * @brief 检查队列是否健康
     * @param type 队列类型
     * @return true 队列健康，false 队列异常
     */
    bool isQueueHealthy(QueueType type) const;

    /**
     * @brief 启用/禁用统计监控
     * @param enabled 是否启用
     */
    void setStatsEnabled(bool enabled);

    /**
     * @brief 设置统计更新间隔
     * @param intervalMs 更新间隔（毫秒）
     */
    void setStatsUpdateInterval(int intervalMs);

    /**
     * @brief 强制更新统计信息
     *
     * 立即更新所有队列的统计信息，主要用于测试场景。
     */
    void forceUpdateStats();

signals:
    /**
     * @brief 队列统计更新信号
     * @param type 队列类型
     * @param stats 统计信息
     */
    void queueStatsUpdated(QueueType type, const QueueStats& stats);

    /**
     * @brief 队列警告信号
     * @param type 队列类型
     * @param message 警告消息
     */
    void queueWarning(QueueType type, const QString& message);

    /**
     * @brief 队列错误信号
     * @param type 队列类型
     * @param error 错误消息
     */
    void queueError(QueueType type, const QString& error);

private slots:
    /**
     * @brief 更新统计信息
     */
    void updateStats();

private:
    /**
     * @brief 更新指定队列的统计信息
     * @param type 队列类型
     */
    void updateQueueStats(QueueType type);

    /**
     * @brief 检查队列健康状态
     * @param type 队列类型
     */
    void checkQueueHealth(QueueType type);

    /**
     * @brief 获取队列名称
     * @param type 队列类型
     * @return 队列名称字符串
     */
    QString getQueueName(QueueType type) const;

private:
    static QueueManager* s_instance;                                    ///< 单例实例
    static QMutex s_instanceMutex;                                      ///< 单例互斥锁

    std::unique_ptr<ThreadSafeQueue<CapturedFrame>> m_captureQueue;     ///< 捕获队列
    std::unique_ptr<ThreadSafeQueue<ProcessedData>> m_processedQueue;   ///< 处理队列

    mutable QMutex m_statsMutex;                                        ///< 统计互斥锁
    QueueStats m_captureStats;                                          ///< 捕获队列统计
    QueueStats m_processedStats;                                        ///< 处理队列统计

    QTimer* m_statsTimer;                                               ///< 统计更新定时器
    bool m_statsEnabled;                                                ///< 统计是否启用
    int m_statsUpdateInterval;                                          ///< 统计更新间隔

    bool m_initialized;                                                 ///< 是否已初始化

    // 健康检查阈值
    static constexpr int QUEUE_WARNING_THRESHOLD = 80;                  ///< 队列警告阈值（百分比）
    static constexpr int QUEUE_ERROR_THRESHOLD = 95;                    ///< 队列错误阈值（百分比）
    static constexpr int MAX_LATENCY_WARNING = 1000;                    ///< 最大延迟警告阈值（毫秒）
};

#endif // QUEUEMANAGER_H