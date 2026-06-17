#pragma once

#include "DataFlowStructures.h"
#include "../../common/core/config/Constants.h"
#include "../../common/core/threading/ThreadSafeQueue.h"
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <memory>
#include "error/RdError.h"

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

    /**
     * @brief 初始化队列管理器
     * @param captureQueueSize 捕获队列最大大小（0表示无限制）
     * @param processedQueueSize 处理队列最大大小（0表示无限制）
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize(
        int captureQueueSize   = CoreConstants::Performance::MAX_QUEUE_SIZE,
        int processedQueueSize = CoreConstants::Performance::MAX_QUEUE_SIZE);

    /**
     * @brief 清理队列管理器
     */
    void cleanup();

    /**
     * @brief 获取队列统计信息
     * @param type 队列类型
     * @return 队列统计信息
     */
    [[nodiscard]] QueueStats getQueueStats(QueueType type) const;

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
     * @brief 强制更新统计信息（主要供测试使用）
     */
    void forceUpdateStats();

    // ==================== 统一的入队和出队接口 ====================

    /**
     * @brief 捕获队列入队（Drain-to-Latest）
     *
     * 队列满时清空所有旧帧，仅保留最新帧入队。
     * 实时流场景下优先保证低延迟，旧帧的显示价值已被新帧替代。
     *
     * @param frame 要入队的捕获帧
     * @return true 入队成功（drain 后始终成功，除非队列未初始化或帧无效）
     */
    [[nodiscard]] bool enqueueCapturedFrame(const CapturedFrame& frame);

    /**
     * @brief 捕获队列出队（FIFO 逐帧出队）
     *
     * Drain-to-Latest 模型下逐帧出队。队列深度由 maxSize=3 控制，
     * 正常运行时队列接近空，波动时 drain 机制丢弃旧帧保证低延迟。
     *
     * @param frame 用于接收出队帧的引用
     * @return true 出队成功，false 队列为空
     */
    [[nodiscard]] bool dequeueCapturedFrame(CapturedFrame& frame);

    /**
     * @brief 处理队列入队（Drain-to-Latest）
     *
     * 队列满时清空所有旧帧，仅保留最新帧入队。
     *
     * @param data 要入队的处理数据
     * @return true 入队成功
     */
    [[nodiscard]] bool enqueueProcessedData(const ProcessedData& data);

    /**
     * @brief 处理队列出队（FIFO 逐帧出队）
     *
     * Drain-to-Latest 模型下逐帧出队，发送端每轮取 1-6 帧(MAX_SEND_BATCH)。
     *
     * @param data 用于接收出队数据的引用
     * @return true 出队成功，false 队列为空
     */
    [[nodiscard]] bool dequeueProcessedData(ProcessedData& data);

    /**
     * @brief 检查处理队列是否已满（流水池背压）
     *
     * 编码线程在出队CQ前调用此方法：PQ满时跳过本轮编码，
     * 等待发送端消化空间后再继续。主动背压，无阻塞无死锁。
     *
     * @return true 队列满，应暂停编码
     */
    [[nodiscard]] bool isProcessedQueueFull() const;

    /**
     * @brief 获取处理队列当前大小
     * @return 当前队列中的帧数
     */
    [[nodiscard]] int getProcessedQueueSize() const;

    /**
     * @brief 获取捕获队列当前大小
     * @return 当前队列中的帧数
     */
    [[nodiscard]] int getCaptureQueueSize() const;

    /**
     * @brief 获取处理队列最大容量
     * @return 队列最大帧数
     */
    [[nodiscard]] int getProcessedQueueMaxSize() const;

signals:
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
    void queueError(const RdError& error);

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
};

