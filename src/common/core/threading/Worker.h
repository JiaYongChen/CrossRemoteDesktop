#ifndef WORKER_H
#define WORKER_H

#include <QtCore/QObject>
#include <QtCore/QMutex>
#include <QtCore/QWaitCondition>
#include <QtCore/QElapsedTimer>
#include <atomic>
#include <memory>

/**
 * @brief 工作线程基类
 *
 * 定义所有工作线程的通用接口和行为模式。
 * 支持启动、停止、暂停、恢复等操作，并提供性能监控功能。
 * 所有具体的Worker类都应该继承此基类。
 */
class Worker : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 工作线程状态枚举
     */
    enum class State {
        Stopped,    ///< 已停止
        Starting,   ///< 启动中
        Running,    ///< 运行中
        Paused,     ///< 已暂停
        Stopping    ///< 停止中
    };

    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit Worker(QObject* parent = nullptr);

    /**
     * @brief 虚析构函数
     */
    virtual ~Worker();

    /**
     * @brief 禁用拷贝构造和赋值操作
     */
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    /**
     * @brief 获取当前状态
     * @return 当前工作线程状态
     */
    State state() const;

    /**
     * @brief 检查是否正在运行
     * @return true 正在运行，false 未运行
     */
    bool isRunning() const;

    /**
     * @brief 检查是否已暂停
     * @return true 已暂停，false 未暂停
     */
    bool isPaused() const;

    /**
     * @brief 检查是否已停止
     * @return true 已停止，false 未停止
     */
    bool isStopped() const;

    /**
     * @brief 获取工作线程名称
     * @return 线程名称
     */
    QString name() const;

    /**
     * @brief 设置工作线程名称
     * @param name 线程名称
     */
    void setName(const QString& name);

    /**
     * @brief 获取性能统计信息
     * @return 性能统计数据
     */
    struct PerformanceStats {
        quint64 totalProcessedItems = 0;    ///< 总处理项目数
        quint64 totalProcessingTime = 0;    ///< 总处理时间（毫秒）
        quint64 averageProcessingTime = 0;  ///< 平均处理时间（毫秒）
        quint64 maxProcessingTime = 0;      ///< 最大处理时间（毫秒）
        quint64 minProcessingTime = UINT64_MAX; ///< 最小处理时间（毫秒）
        double itemsPerSecond = 0.0;        ///< 每秒处理项目数
        quint64 uptime = 0;                 ///< 运行时间（毫秒）
    };

    PerformanceStats getPerformanceStats() const;

    /**
     * @brief 重置性能统计
     */
    void resetPerformanceStats();

    // 允许线程管理器访问受保护的生命周期方法，以便在销毁阶段执行安全的跨线程清理
    friend class ThreadManager;

public slots:
    /**
     * @brief 启动工作线程
     *
     * 异步启动工作线程，启动完成后会发出started信号。
     */
    virtual void start();

    /**
     * @brief 停止工作线程
     *
     * 异步停止工作线程，停止完成后会发出stopped信号。
     *
     * @param waitForFinish 是否等待当前任务完成
     */
    virtual void stop(bool waitForFinish = true);

    /**
     * @brief 暂停工作线程（线程安全）
     *
     * 可在任意线程调用，仅设置暂停请求标志，由工作线程在安全点切换状态。
     */
    virtual void pause();

    /**
     * @brief 恢复工作线程（线程安全）
     *
     * 清除暂停请求并唤醒等待的工作线程。
     */
    virtual void resume();

    /**
     * @brief 请求处理单个任务
     *
     * 子类应该重写此方法来实现具体的处理逻辑。
     * 此方法在工作线程中被调用。
     */
    virtual void processTask() = 0;

signals:
    /**
     * @brief 工作线程启动信号
     */
    void started();

    /**
     * @brief 工作线程停止信号
     */
    void stopped();

    /**
     * @brief 工作线程暂停信号
     */
    void paused();

    /**
     * @brief 工作线程恢复信号
     */
    void resumed();

    /**
     * @brief 错误信号
     * @param error 错误信息
     */
    void errorOccurred(const QString& error);

    /**
     * @brief 状态变化信号
     * @param newState 新状态
     * @param oldState 旧状态
     */
    void stateChanged(Worker::State newState, Worker::State oldState);

    /**
     * @brief 性能统计更新信号
     * @param stats 性能统计数据
     */
    void performanceStatsUpdated(const Worker::PerformanceStats& stats);

protected:
    /**
     * @brief 设置工作线程状态
     * @param newState 新状态
     */
    void setState(State newState);

    /**
     * @brief 检查是否应该停止处理
     * @return true 应该停止，false 继续处理
     */
    bool shouldStop() const;

    /**
     * @brief 等待暂停状态结束
     *
     * 如果当前处于暂停状态，此方法会阻塞直到恢复或停止。
     */
    void waitIfPaused();

    /**
     * @brief 开始性能计时
     *
     * 在处理任务前调用此方法开始计时。
     */
    void startPerformanceTiming();

    /**
     * @brief 结束性能计时
     *
     * 在处理任务后调用此方法结束计时并更新统计。
     */
    void endPerformanceTiming();

    /**
     * @brief 发出错误信号
     * @param error 错误信息
     */
    void emitError(const QString& error);

    /**
     * @brief 初始化工作线程
     *
     * 子类可以重写此方法来执行初始化操作。
     * 此方法在工作线程启动时被调用。
     *
     * @return true 初始化成功，false 初始化失败
     */
    virtual bool initialize();

    /**
     * @brief 清理工作线程
     *
     * 子类可以重写此方法来执行清理操作。
     * 此方法在工作线程停止时被调用。
     */
    virtual Q_INVOKABLE void cleanup();

    /**
     * @brief 主工作循环
     *
     * 子类可以重写此方法来实现自定义的工作循环。
     * 默认实现会持续调用processTask()直到停止。
     */
    virtual void workLoop();

protected slots:
    /**
     * @brief 执行启动流程（内部使用）
     */
    void doStart();

    /**
     * @brief 执行停止流程（内部使用）
     */
    void doStop();

private:
    void updatePerformanceStats(quint64 processingTime);

private:
    mutable QMutex m_stateMutex;        ///< 状态互斥锁
    std::atomic<State> m_state;         ///< 当前状态
    std::atomic<bool> m_stopRequested;  ///< 停止请求标志
    std::atomic<bool> m_pauseRequested; ///< 暂停请求标志

    QMutex m_pauseMutex;                ///< 暂停互斥锁
    QWaitCondition m_pauseCondition;    ///< 暂停条件变量

    QString m_name;                     ///< 线程名称

    // 性能统计相关
    mutable QMutex m_statsMutex;        ///< 统计互斥锁
    PerformanceStats m_stats;           ///< 性能统计数据
    QElapsedTimer m_processingTimer;    ///< 处理计时器
    QElapsedTimer m_uptimeTimer;        ///< 运行时间计时器

    bool m_waitForFinish;               ///< 是否等待完成
};

#endif // WORKER_H