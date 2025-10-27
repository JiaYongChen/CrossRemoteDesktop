#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QVariant>
#include <QtCore/QPointer>
#include <QtGui/QImage>
#include <memory>
#include <atomic>
#include "CaptureConfig.h"

// 前向声明
class ScreenCaptureWorker;
class ThreadManager;
class QTimer;

/**
 * @brief 多线程屏幕捕获管理器
 * 
 * 重构后的ScreenCapture类作为多线程架构的协调器，
 * 管理ScreenCaptureWorker工作线程，
 * 通过Qt信号/槽直接传递帧，降低耦合、减少阻塞点。
 */
class ScreenCapture : public QObject
{
    Q_OBJECT

public:
    explicit ScreenCapture(QObject *parent = nullptr);
    ~ScreenCapture();

    // 捕获控制方法
    void startCapture();
    void stopCapture();
    bool isCapturing() const;
    
    // 配置管理方法 - 统一通过CaptureConfig设置
    void updateCaptureConfig(const CaptureConfig& config);
    CaptureConfig getCaptureConfig() const;

    // 性能统计结构体
    struct PerformanceStats {
        quint64 totalFramesCaptured = 0;
        quint64 totalFramesProcessed = 0;
        quint64 droppedFrames = 0;
        double captureFrameRate = 0.0;
        double processingFrameRate = 0.0;
        quint64 averageCaptureTime = 0;
        quint64 averageProcessingTime = 0;
    };
    
    PerformanceStats getPerformanceStats() const;
    void resetPerformanceStats();

signals:
    /**
     * @brief 捕获错误信号
     * @param error 错误描述
     */
    void captureError(const QString& error);

    /**
     * @brief 性能统计更新信号
     * @param stats 性能统计数据
     */
    void performanceStatsUpdated(const PerformanceStats& stats);

private slots:
    /**
     * @brief 处理捕获错误
     * @param error 错误信息
     */
    void onCaptureError(const QString& error);

    /**
     * @brief 更新性能统计
     */
    void updatePerformanceStats();
    
    /**
     * @brief 处理线程启动信号
     * @param name 线程名称
     */
    void onThreadStarted(const QString& name);
    
    /**
     * @brief 处理线程停止信号
     * @param name 线程名称
     */
    void onThreadStopped(const QString& name);
    
    /**
     * @brief 处理线程错误信号
     * @param name 线程名称
     * @param error 错误信息
     */
    void onThreadError(const QString& name, const QString& error);
    
    /**
     * @brief 处理线程重启信号
     * @param name 线程名称
     * @param restartCount 重启次数
     */
    void onThreadRestarted(const QString& name, int restartCount);

private:
    // 线程管理方法
    /**
     * @brief 初始化工作线程
     */
    bool initializeThreads();
    
    /**
     * @brief 清理工作线程
     */
    void cleanupThreads();
    
    /**
     * @brief 配置工作线程参数
     */
    void configureWorkers();

private:
    // 成员变量
    ThreadManager* m_threadManager;                                    ///< 线程管理器
    QPointer<ScreenCaptureWorker> m_captureWorker;                     ///< 非拥有指针，由ThreadManager管理生命周期
    
    // 状态控制
    std::atomic<bool> m_isCapturing;                                   ///< 捕获状态
    
    // 配置参数（线程安全）- 统一使用CaptureConfig管理
    mutable QMutex m_configMutex;                                      ///< 配置互斥锁
    CaptureConfig m_captureConfig;                                     ///< 捕获配置结构
    
    // 性能统计
    mutable QMutex m_statsMutex;                                       ///< 统计数据互斥锁
    PerformanceStats m_performanceStats;                               ///< 性能统计数据
    QTimer* m_statsTimer;                                              ///< 统计更新定时器
    
    // 常量定义
    static constexpr int STATS_UPDATE_INTERVAL = 1000;                 ///< 统计更新间隔（毫秒）
};

#endif // SCREENCAPTURE_H