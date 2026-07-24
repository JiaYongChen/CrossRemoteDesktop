#pragma once

#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtGui/QImage>
#include <QtGui/QScreen>

#include <atomic>
#include <chrono>
#include <memory>

#include "common/threading/Worker.h"
#include "CaptureConfig.h"
#include "IScreenCapture.h"

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

    /**
     * @brief 开始捕获
     *
     * 设置内部原子标志 m_isCapturing 为 true，并按需初始化并启动捕获定时器。
     * 线程安全：m_isCapturing 为原子类型，直接设置即可。
     */
    Q_INVOKABLE void startCapturing();

    /**
     * @brief 停止捕获
     *
     * 将 m_isCapturing 置为 false，停止捕获定时器并断开其超时信号，
     * 避免单元测试环境下产生多余的定时器触发与潜在告警。
     */
    Q_INVOKABLE void stopCapturing();

signals:
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

private:
    // 核心捕获方法
    QImage captureScreen();
    QImage captureScreenRegion(const QRect& region);

    // 帧率和时序控制
    void calculateFrameDelay();
    bool shouldCaptureFrame();

    /// 独立于帧捕获的光标位置高频采样（帧间隔期间调用）
    void sampleCursorPosition();

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
    QTimer* m_captureTimer{ nullptr };                    ///< 捕获定时器（仅在未启动Worker线程或测试环境下使用）
    std::chrono::steady_clock::time_point m_lastCaptureTime; ///< 上次捕获时间
    std::chrono::milliseconds m_frameDelay{ 33 }; ///< 帧间延迟

    // 光标独立采样（与帧率解耦）
    std::chrono::steady_clock::time_point m_lastCursorSampleTime;

    // 帧计数器
    quint64 m_totalFramesCaptured{0};          ///< 总捕获帧数（用于生成帧ID）

    // 屏幕相关
    QScreen* m_primaryScreen;                  ///< 主屏幕指针
    QRect m_screenGeometry;                    ///< 屏幕几何信息

    // 错误处理
    std::atomic<int> m_errorCount{ 0 };
    std::atomic<bool> m_recoveryMode{ false };
    QString m_lastError;

    // 常量定义（已迁移至 CaptureConstants.h / ProcessingConstants.h）

    // 硬件加速屏幕捕获引擎（通过 IScreenCapture 接口）
    std::unique_ptr<IScreenCapture> m_screenCapture;
    bool m_hardwareCaptureAvailable = false;
    int m_captureReinitAttempts = 0;
};

