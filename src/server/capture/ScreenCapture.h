#pragma once

#include <QtCore/QObject>
#include <QtCore/QMutex>
#include <QtCore/QPointer>
#include "error/RdError.h"
#include <atomic>
#include "CaptureConfig.h"

// 前向声明
class ScreenCaptureWorker;
class ThreadManager;
class QueueManager;

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
    explicit ScreenCapture(ThreadManager* threadMgr, QueueManager* queueMgr, QObject *parent = nullptr);
    ~ScreenCapture();

    // 捕获控制方法
    void startCapture();
    void stopCapture();
    bool isCapturing() const;
    
    // 配置管理方法 - 统一通过CaptureConfig设置
    void updateCaptureConfig(const CaptureConfig& config);
    CaptureConfig getCaptureConfig() const;

signals:
    /**
     * @brief 捕获错误信号
     * @param error 错误描述
     */
    void captureError(const RdError& error);

private slots:
    /**
     * @brief 处理捕获错误
     * @param error 错误信息
     */
    void onCaptureError(const RdError& error);

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
    void onThreadError(const RdError& error);
    
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
    QueueManager* m_queueManager;                                      ///< 队列管理器
    QPointer<ScreenCaptureWorker> m_captureWorker;                     ///< 非拥有指针，由ThreadManager管理生命周期
    
    // 状态控制
    std::atomic<bool> m_isCapturing;                                   ///< 捕获状态
    
    // 配置参数（线程安全）- 统一使用CaptureConfig管理
    mutable QMutex m_configMutex;                                      ///< 配置互斥锁
    CaptureConfig m_captureConfig;                                     ///< 捕获配置结构
};

