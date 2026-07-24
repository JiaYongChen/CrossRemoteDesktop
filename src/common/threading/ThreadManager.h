#pragma once

#include <memory>

#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QThread>

#include "common/error/RdError.h"
#include "common/logging/LoggingCategories.h"
#include "common/threading/Worker.h"

/**
 * @brief 线程管理器类
 *
 * 负责管理所有工作线程的生命周期，包括创建、启动、停止和销毁线程。
 * 提供线程池管理和资源管理功能。
 */
class ThreadManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 线程信息结构
     */
    struct ThreadInfo {
        QString name;                           ///< 线程名称
        QThread* thread;                        ///< 线程对象
        Worker* worker;                         ///< 工作对象
        bool autoRestart;                       ///< 是否自动重启
        int restartCount;                       ///< 重启次数
        int maxRestarts;                        ///< 最大重启次数
        bool stopRequested = false;             ///< 是否为主动停止（手动stop/destroy触发），用于跳过自动重启
        
        // 析构函数，负责清理资源
        ~ThreadInfo() {
            // 析构时尽量避免强制终止线程导致的不确定崩溃：
            // 1) 若线程仍在运行，尝试请求退出并短暂等待；
            // 2) 不使用 terminate()，由上层的 stopThread/destroyThread 保证线程已结束；
            if (thread) {
                if (thread->isRunning()) {
                    // 尝试优雅退出
                    thread->quit();
                    thread->wait(800);
                }
                // 若仍在运行，避免直接delete导致崩溃，改为延迟删除
                if (thread->isRunning()) {
                    qCWarning(lcCoreThreading) << "ThreadInfo destructor: QThread is still running; deferring deletion for thread" << name;
                    // Worker同样延迟删除，防止跨线程析构
                    if (worker) {
                        QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
                        worker = nullptr;
                    }
                    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
                    thread = nullptr;
                    return; // 延迟删除已安排，避免下面的直接delete
                }
            }

            if (worker) {
                delete worker;
                worker = nullptr;
            }
            if (thread) {
                delete thread;
                thread = nullptr;
            }
        }
    };

    /**
     * @brief 析构函数
     */
    ~ThreadManager();

    /**
     * @brief 禁用拷贝构造和赋值操作
     */
    ThreadManager(const ThreadManager&) = delete;
    ThreadManager& operator=(const ThreadManager&) = delete;

    /**
     * @brief 创建并注册工作线程
     * 
     * @param name 线程名称（必须唯一）
     * @param worker 工作对象（管理器会接管所有权）
     * @param autoStart 是否自动启动
     * @param autoRestart 是否自动重启
     * @param maxRestarts 最大重启次数（-1表示无限制）
     * @return true 创建成功，false 创建失败（名称重复等）
     */
    [[nodiscard]] bool createThread(const QString& name,
                     std::unique_ptr<Worker> worker,
                     bool autoStart = false,
                     bool autoRestart = false,
                     int maxRestarts = 3);

    /**
     * @brief 启动指定线程
     * @param name 线程名称
     * @return true 启动成功，false 启动失败
     */
    [[nodiscard]] bool startThread(const QString& name);

    /**
     * @brief 停止指定线程
     * @param name 线程名称
     * @param waitForFinish 是否等待当前任务完成
     * @return true 停止成功，false 停止失败
     */
    [[nodiscard]] bool stopThread(const QString& name, bool waitForFinish = true);

    /**
     * @brief 暂停指定线程
     * @param name 线程名称
     * @return true 暂停成功，false 暂停失败
     */
    [[nodiscard]] bool pauseThread(const QString& name);

    /**
     * @brief 恢复指定线程
     * @param name 线程名称
     * @return true 恢复成功，false 恢复失败
     */
    [[nodiscard]] bool resumeThread(const QString& name);

    /**
     * @brief 销毁指定线程
     * @param name 线程名称
     * @return true 销毁成功，false 销毁失败
     */
    [[nodiscard]] bool destroyThread(const QString& name);

    /**
     * @brief 停止所有线程
     * @param waitForFinish 是否等待当前任务完成
     */
    void stopAllThreads(bool waitForFinish = true);

    /**
     * @brief 销毁所有线程
     */
    void destroyAllThreads();

    /**
     * @brief 检查线程是否存在
     * @param name 线程名称
     * @return true 存在，false 不存在
     */
    [[nodiscard]] bool hasThread(const QString& name) const;

    /**
     * @brief 检查指定线程是否处于运行状态
     * @param name 线程名称
     * @return true 线程正在运行，false 不在运行或不存在
     */
    [[nodiscard]] bool isThreadRunning(const QString& name) const;

    /**
     * @brief 获取线程信息
     * @param name 线程名称
     * @return 线程信息，如果线程不存在则返回nullptr
     */
    [[nodiscard]] const ThreadInfo* getThreadInfo(const QString& name) const;

    /**
     * @brief 获取所有线程名称
     * @return 线程名称列表
     */
    [[nodiscard]] QStringList getThreadNames() const;

    /**
     * @brief 获取指定线程的Worker对象
     * @param name 线程名称
     * @return Worker对象指针，如果线程不存在则返回nullptr
     */
    [[nodiscard]] Worker* getWorker(const QString& name) const;

signals:
    /**
     * @brief 线程启动信号
     * @param name 线程名称
     */
    void threadStarted(const QString& name);

    /**
     * @brief 线程停止信号
     * @param name 线程名称
     */
    void threadStopped(const QString& name);

    /**
     * @brief 线程错误信号
     * @param name 线程名称
     * @param error 错误信息
     */
    void threadError(const RdError& error);

    /**
     * @brief 线程重启信号
     * @param name 线程名称
     * @param restartCount 重启次数
     */
    void threadRestarted(const QString& name, int restartCount);

private slots:
    /**
     * @brief 处理工作线程启动
     */
    void onWorkerStarted();

    /**
     * @brief 处理工作线程停止
     */
    void onWorkerStopped();

    /**
     * @brief 处理工作线程暂停
     */
    void onWorkerPaused();

    /**
     * @brief 处理工作线程恢复
     */
    void onWorkerResumed();

    /**
     * @brief 处理工作线程错误
     * @param error 错误信息
     */
    void onWorkerError(const RdError& error);

public:
    /**
     * @brief 构造函数
     *
     * 构造实例并通过依赖注入传递到需要的组件中（非单例）。
     *
     * @param parent 父对象
     */
    explicit ThreadManager(QObject *parent = nullptr);

private:

    /**
     * @brief 查找线程信息
     * @param name 线程名称
     * @return 线程信息指针，如果不存在则返回nullptr
     */
    ThreadInfo* findThreadInfo(const QString& name);

    /**
     * @brief 查找线程信息（常量版本）
     * @param name 线程名称
     * @return 线程信息指针，如果不存在则返回nullptr
     */
    const ThreadInfo* findThreadInfo(const QString& name) const;

    /**
     * @brief 获取Worker对象对应的线程名称
     * @param worker Worker对象指针
     * @return 线程名称，如果找不到则返回空字符串
     */
    QString getThreadNameByWorker(Worker* worker) const;

    /**
     * @brief 连接Worker信号
     * @param worker Worker对象指针
     */
    void connectWorkerSignals(Worker* worker);

    /**
     * @brief 断开Worker信号
     * @param worker Worker对象指针
     */
    void disconnectWorkerSignals(Worker* worker);

    /**
     * @brief 尝试自动重启线程
     * @param name 线程名称
     */
    void tryAutoRestart(const QString& name);

private:
    mutable QMutex m_mutex;                 ///< 互斥锁
    QHash<QString, std::shared_ptr<ThreadInfo>> m_threads; ///< 线程信息映射
};

