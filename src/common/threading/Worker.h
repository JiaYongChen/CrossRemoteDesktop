#pragma once

#include <QtCore/QObject>
#include <QtCore/QMutex>
#include <QtCore/QWaitCondition>
#include <atomic>
#include "error/RdError.h"

/**
 * @brief 工作线程基类
 *
 * 定义所有工作线程的通用接口和行为模式。
 * 支持启动、停止、暂停、恢复等操作。
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
    [[nodiscard]] State state() const;

    /**
     * @brief 检查是否正在运行
     * @return true 正在运行，false 未运行
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @brief 检查是否已暂停
     * @return true 已暂停，false 未暂停
     */
    [[nodiscard]] bool isPaused() const;

    /**
     * @brief 获取工作线程名称
     * @return 线程名称
     */
    [[nodiscard]] QString name() const;

    /**
     * @brief 设置工作线程名称
     * @param name 线程名称
     */
    void setName(const QString& name);

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
    void errorOccurred(const RdError& error);

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
    [[nodiscard]] bool shouldStop() const;

    /**
     * @brief 等待暂停状态结束
     *
     * 如果当前处于暂停状态，此方法会阻塞直到恢复或停止。
     */
    void waitIfPaused();

    /**
     * @brief 发出错误信号
     * @param error 错误信息
     */
    void emitError(const QString& error);

    /**
     * @brief Hint to workLoop whether processTask() performed useful work.
     *
     * Call setDidWork(true) when processTask() actually processed data;
     * call setDidWork(false) when it found nothing to do (empty queue, etc.).
     * workLoop uses this hint: if didWork==true, it skips the idle sleep
     * and immediately re-enters processTask() for maximum throughput.
     *
     * Subclasses that never call this method get the default behavior
     * (always sleep 1ms between iterations — backward compatible).
     *
     * @param didWork true if processTask() processed data, false if idle
     */
    void setDidWork(bool didWork);

    /**
     * @brief 初始化工作线程
     *
     * 子类可以重写此方法来执行初始化操作。
     * 此方法在工作线程启动时被调用。
     *
     * @return true 初始化成功，false 初始化失败
     */
    [[nodiscard]] virtual bool initialize();

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
    mutable QMutex m_nameMutex;         ///< 线程名称互斥锁（仅保护 m_name）
    std::atomic<State> m_state;         ///< 当前状态
    std::atomic<bool> m_stopRequested;  ///< 停止请求标志
    std::atomic<bool> m_pauseRequested; ///< 暂停请求标志

    QMutex m_pauseMutex;                ///< 暂停互斥锁
    QWaitCondition m_pauseCondition;    ///< 暂停条件变量

    QString m_name;                     ///< 线程名称

    // Adaptive sleep: when a subclass calls setDidWork(false), workLoop
    // sleeps 1ms; when setDidWork(true), it skips the sleep. Subclasses
    // that never call setDidWork() always sleep (m_adaptiveSleepEnabled stays false).
    std::atomic<bool> m_adaptiveSleepEnabled{false}; ///< Whether subclass opted in
    std::atomic<bool> m_lastDidWork{false};          ///< Last processTask() work hint
};