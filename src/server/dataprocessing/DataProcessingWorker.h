#pragma once

#include <QtCore/QFutureWatcher>
#include <QtCore/QObject>

#include <atomic>
#include <memory>
#include <vector>

#include "common/config/ProcessingConstants.h"
#include "common/threading/Worker.h"
#include "server/dataflow/DataFlowStructures.h"

template<typename T>
class ThreadSafeQueue;

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

    /// 设置 JPEG 编码质量（线程安全，可在任意线程调用）
    void setJpegQuality(int quality);
    /// 获取当前 JPEG 编码质量
    int jpegQuality() const;

    /// 设置色度子采样策略（线程安全，可在任意线程调用）
    /// @param colorDepth 客户端请求的色深（16/24/32），内部映射为 TJSAMP_*
    void setChromaSubsampling(int colorDepth);

    /**
     * @brief 设置处理超时时间
     * @param timeoutMs 超时时间（毫秒）
     */
    void setProcessingTimeout(int timeoutMs);

    /**
     * @brief 停止工作线程
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

protected:
    /**
     * @brief 初始化工作线程
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize() override;

public:
    /// 设置输入/输出队列指针（DI 注入，必须在 Worker 启动前调用）
    void setQueues(ThreadSafeQueue<CapturedFrame>* captureQueue,
                   ThreadSafeQueue<ProcessedData>* processedQueue);

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

private:
    /**
     * @brief 批量并行处理多个帧
     * @param frames 待处理的帧列表
     * @return 处理成功的帧数
     */
    /// 异步非阻塞并行编码：提交后立即返回，编码完成后通过 QFutureWatcher 回调入队
    void processBatchAsync(std::vector<CapturedFrame>&& frames);

    /// QFutureWatcher 回调：上一批异步编码完成时收集结果并入队
    void onAsyncBatchFinished();

    /**
     * @brief 并行编码单帧图像（线程安全的静态方法）
     * @param image 图像数据
     * @param frameId 帧ID
     * @param quality JPEG质量 (0-100)
     * @param scaleFactor 缩放因子 (0.1-1.0)
     * @return 处理后的数据
     */
    static ProcessedData encodeImageParallel(const QImage& image, quint64 frameId,
                                             int quality = ProcessingConstants::DefaultJpegQuality,
                                             double scaleFactor = 1.0,
                                             int chromaSubsampling = ProcessingConstants::DefaultChromaSubsampling);

private:
    ThreadSafeQueue<CapturedFrame>* m_captureQueue = nullptr;          ///< 输入队列（捕获帧）
    ThreadSafeQueue<ProcessedData>* m_processedQueue = nullptr;        ///< 输出队列（处理后数据）

    // 配置参数
    int m_processingTimeout{ProcessingConstants::DefaultProcessingTimeoutMs};  ///< 处理超时时间（毫秒）
    std::atomic<int> m_jpegQuality{ProcessingConstants::DefaultJpegQuality};  ///< JPEG 编码质量（线程安全）
    std::atomic<int> m_chromaSubsampling{ProcessingConstants::DefaultChromaSubsampling};                    ///< JPEG 色度子采样（线程安全）

    // 异步非阻塞编码（替代 waitForFinished 阻断）
    QFutureWatcher<ProcessedData>* m_asyncWatcher = nullptr;            ///< 异步编码观察器
    std::atomic<int> m_inFlightBatches{0};                              ///< 当前飞行中的批次数
    std::shared_ptr<std::vector<CapturedFrame>> m_inFlightFrames;       ///< 飞行中批次的帧数据（智能指针管理生命周期）
    // 常量已迁移至 CaptureConstants.h / ProcessingConstants.h
};

