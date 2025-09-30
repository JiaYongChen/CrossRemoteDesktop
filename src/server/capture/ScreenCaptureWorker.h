#ifndef SCREENCAPTUREWORKER_H
#define SCREENCAPTUREWORKER_H

#include "../../common/core/threading/Worker.h"
#include "../../common/core/threading/ThreadSafeQueue.h"
#include "../dataprocessing/DataProcessing.h"
#include "CaptureConfig.h"
#include <QtGui/QImage>
#include <QtGui/QScreen>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QElapsedTimer>
#include <QtCore/QBuffer>
#include <memory>
#include <atomic>
#include <chrono>
#include <deque>

/**
 * @brief 屏幕捕获工作线程类
 * 
 * 继承Worker基类，在独立线程中执行屏幕捕获操作。
 * 支持帧率控制、质量调整、错误处理和性能监控。
 * 改造说明：移除了对ThreadSafeQueue的依赖，改为仅通过信号输出帧。
 */
class ScreenCaptureWorker : public Worker
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数（重构后不再依赖输出队列）
     * @param parent 父对象
     */
    explicit ScreenCaptureWorker(QObject *parent = nullptr);

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

    // 数据验证配置方法
    void setDataValidationEnabled(bool enabled);
    bool isDataValidationEnabled() const;
    quint64 getLastFrameChecksum() const;
    
    

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
     * @brief 帧捕获完成信号
     * @param frame 捕获的图像
     * @param timestamp 捕获时间戳
     */
    void frameCaptured(const QImage& frame, qint64 timestamp);

    /**
     * @brief 捕获统计更新信号（内部使用）
     * @param stats 统计信息
     */
    void captureStatsUpdated(const CaptureStats& stats);

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
    
    // 屏幕变化检测方法已移除
    
    // 性能监控方法
    void recordCaptureTime(std::chrono::milliseconds time);
    void updateFrameRate();
    void monitorResourceUsage();
    
    // 错误处理方法
    void handleCaptureError(const QString& error);
    bool recoverFromError();

private:
    // 配置参数 (线程安全)
    mutable QMutex m_configMutex;
    CaptureConfig m_config;
    
    // 捕获状态
    std::atomic<bool> m_isCapturing{false};
    std::atomic<bool> m_configChanged{false};
    
    // 时序控制
    QTimer* m_statsTimer{nullptr};                      ///< 统计更新定时器
    QTimer* m_captureTimer{nullptr};                    ///< 捕获定时器（仅在未启动Worker线程或测试环境下使用）
    std::chrono::steady_clock::time_point m_lastCaptureTime; ///< 上次捕获时间
    std::chrono::milliseconds m_frameDelay{33}; ///< 帧间延迟
    
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
    std::atomic<int> m_errorCount{0};
    std::atomic<bool> m_recoveryMode{false};
    QString m_lastError;
    
    // 数据验证相关
    std::unique_ptr<DataValidator> m_dataValidator;  ///< 数据验证器
    bool m_dataValidationEnabled{false};            ///< 数据验证开关
    quint64 m_lastFrameChecksum{0};                 ///< 最后一帧的校验和
    
    // 常量定义
    static constexpr int STATS_UPDATE_INTERVAL = 1000;     ///< 统计更新间隔(ms)
    static constexpr int MAX_CAPTURE_TIME_HISTORY = 100;   ///< 最大捕获时间历史记录数
    static constexpr int MAX_FRAME_TIMESTAMP_HISTORY = 60; ///< 最大帧时间戳历史记录数
    static constexpr int MAX_ERROR_COUNT = 10;             ///< 最大错误计数
    static constexpr double MIN_QUALITY = 0.1;            ///< 最小质量值
    static constexpr double MAX_QUALITY = 1.0;            ///< 最大质量值
    static constexpr int MIN_FRAME_RATE = 1;              ///< 最小帧率
    static constexpr int MAX_FRAME_RATE = 120;            ///< 最大帧率
};

#endif // SCREENCAPTUREWORKER_H