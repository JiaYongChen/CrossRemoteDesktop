#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>

#include <QtCore/QElapsedTimer>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtGui/QImage>
#include <QtGui/QScreen>

#include "../../common/threading/Worker.h"
#include "../dataflow/DataFlowStructures.h"
#include "CaptureConfig.h"
#include "../../common/network/Protocol.h"
#ifdef Q_OS_WIN
#include "DxgiCapture.h"
#endif

class QueueManager;

/**
 * @brief 屏幕捕获工作线程类
 *
 * 继承Worker基类，在独立线程中执行屏幕捕获操作。
 * 支持帧率控制、质量调整、错误处理和性能监控。
 * 改造说明：移除了对ThreadSafeQueue的依赖，改为仅通过信号输出帧。
 */
class ScreenCaptureWorker : public Worker {
    Q_OBJECT

public:
    /**
     * @brief 构造函数（支持队列作为生产者）
     * @param queueManager 队列管理器，用于将捕获的帧放入队列
     * @param parent 父对象
     */
    explicit ScreenCaptureWorker(QueueManager* queueManager = nullptr, QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~ScreenCaptureWorker() override;

    /**
     * @brief 禁用拷贝构造和赋值操作
     */
    ScreenCaptureWorker(const ScreenCaptureWorker&) = delete;
    ScreenCaptureWorker& operator=(const ScreenCaptureWorker&) = delete;

    // 配置管理方法
    void updateConfig(const CaptureConfig& config);
    CaptureConfig getCurrentConfig() const;

    // 统计信息获取（内部使用）
    CaptureStats getCaptureStats() const;

    /**
     * @brief 开始捕获
     *
     * 设置内部原子标志m_isCapturing为true，并按需连接并启动统计定时器。
     * 线程安全：m_isCapturing为原子类型，直接设置即可。
     */
    Q_INVOKABLE void startCapturing();

    /**
     * @brief 停止捕获
     *
     * 将m_isCapturing置为false，并停止统计定时器且断开其超时信号，
     * 避免单元测试环境下产生多余的定时器触发与潜在告警。
     */
    Q_INVOKABLE void stopCapturing();

signals:
    /**
     * @brief 捕获统计更新信号（内部使用）
     * @param stats 统计信息
     */
    void captureStatsUpdated(const CaptureStats& stats);

    /**
     * @brief 光标更新信号
     * @param cursor 光标形状数据（含热点和RGBA像素）
     */
    void cursorUpdateReady(const CursorMessage& cursor);

    /**
     * @brief 帧已入队信号（驱动 FrameBroadcaster 拉取）
     *
     * 每次成功 enqueueCapturedFrame 后发出。FrameBroadcaster
     * 通过 QueuedConnection 接收，dequeue 最新帧并广播到各 session。
     */
    void frameEnqueued();

protected:
    /**
     * @brief 初始化工作线程
     * @return 是否初始化成功
     */
    bool initialize() override;

    /**
     * @brief 清理工作线程资源
     */
    void cleanup() override;

    /**
     * @brief 执行单次任务处理
     *
     * 实现Worker基类的纯虚函数，执行一次屏幕捕获操作。
     */
    void processTask() override;

    /**
     * @brief 定时器触发的捕获操作
     */
    void performCapture();

private slots:
    /**
     * @brief 更新统计信息
     */
    void updateStats();

private:
    // 核心捕获方法
    QImage captureScreen();
    QImage captureScreenRegion(const QRect& region);

    // 帧率和时序控制
    void calculateFrameDelay();
    bool shouldCaptureFrame();

    /// 独立于帧捕获的光标位置高频采样（帧间隔期间调用）
    void sampleCursorPosition();

    // 性能监控方法
    void recordCaptureTime(std::chrono::milliseconds time);
    void updateFrameRate();
    void monitorResourceUsage();

    // 错误处理方法
    void handleCaptureError(const QString& error);
    bool recoverFromError();

private:
    // 队列管理
    QueueManager* m_queueManager{ nullptr };  ///< 队列管理器，用于将捕获的帧放入队列

    // 配置相关
    mutable QMutex m_configMutex;
    CaptureConfig m_config;

    // 捕获状态
    std::atomic<bool> m_isCapturing{ false };
    std::atomic<bool> m_configChanged{ false };
    std::atomic<bool> m_initialized{ false };   ///< 防重复初始化
    std::atomic<bool> m_cleanedUp{ false };     ///< 防重复清理

    // 时序控制
    QTimer* m_statsTimer{ nullptr };                      ///< 统计更新定时器
    QTimer* m_captureTimer{ nullptr };                    ///< 捕获定时器（仅在未启动Worker线程或测试环境下使用）
    std::chrono::steady_clock::time_point m_lastCaptureTime; ///< 上次捕获时间
    std::chrono::milliseconds m_frameDelay{ 33 }; ///< 帧间延迟

    // 光标独立采样（与帧率解耦）
    std::chrono::steady_clock::time_point m_lastCursorSampleTime;

    // 性能统计
    mutable QMutex m_statsMutex;
    CaptureStats m_stats;
    QElapsedTimer m_captureTimer_perf;         ///< 性能计时器
    std::deque<std::chrono::milliseconds> m_captureTimeHistory; ///< 捕获时间历史
    std::deque<qint64> m_frameTimestamps;      ///< 帧时间戳历史

    // 屏幕相关
    QScreen* m_primaryScreen;                  ///< 主屏幕指针
    QRect m_screenGeometry;                    ///< 屏幕几何信息

    // 错误处理
    std::atomic<int> m_errorCount{ 0 };
    std::atomic<bool> m_recoveryMode{ false };
    QString m_lastError;

    // 常量定义（已迁移至 CaptureConstants.h / ProcessingConstants.h）

    // DXGI Desktop Duplication engine (Windows only)
#ifdef Q_OS_WIN
    std::unique_ptr<DxgiCapture> m_dxgiCapture;    ///< DXGI capture engine
    bool m_dxgiAvailable = false;                   ///< Whether DXGI init succeeded
    int m_dxgiReinitAttempts = 0;                   ///< Consecutive reinit attempts
#endif
};

