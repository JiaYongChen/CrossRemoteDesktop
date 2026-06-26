#include "Worker.h"
#include "../logging/LoggingCategories.h"
#include <QtCore/QThread>
#include <QtCore/QMutexLocker>
#include <QtCore/QTimer>
#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>

Worker::Worker(QObject* parent)
    : QObject(parent)
    , m_state(State::Stopped)
    , m_stopRequested(false)
    , m_pauseRequested(false)
    , m_name("Worker")
    , m_waitForFinish(true) {
    // 初始化性能统计
    resetPerformanceStats();
}

Worker::~Worker() {
    // 析构阶段不再主动干预线程停止，避免跨线程调度与等待导致的潜在崩溃。
    // 线程的生命周期由 ThreadManager 显式管理（stopThread/destroyThread 中确保线程已退出）。
}

Worker::State Worker::state() const {
    return m_state.load();
}

bool Worker::isRunning() const {
    State currentState = m_state.load();
    return currentState == State::Running || currentState == State::Starting;
}

bool Worker::isPaused() const {
    return m_state.load() == State::Paused;
}

bool Worker::isStopped() const {
    State currentState = m_state.load();
    // 修正语义：仅当状态为 Stopped 时返回 true；Stopping 表示正在停止过程中，尚未真正完成
    return currentState == State::Stopped;
}

QString Worker::name() const {
    QMutexLocker locker(&m_nameMutex);
    return m_name;
}

void Worker::setName(const QString& name) {
    QMutexLocker locker(&m_nameMutex);
    m_name = name;
}

Worker::PerformanceStats Worker::getPerformanceStats() const {
    QMutexLocker locker(&m_statsMutex);
    PerformanceStats stats = m_stats;

    // 更新运行时间
    if ( m_uptimeTimer.isValid() ) {
        stats.uptime = m_uptimeTimer.elapsed();
    }

    // 计算每秒处理项目数
    if ( stats.uptime > 0 ) {
        stats.itemsPerSecond = (stats.totalProcessedItems * 1000.0) / stats.uptime;
    }

    return stats;
}

void Worker::resetPerformanceStats() {
    QMutexLocker locker(&m_statsMutex);
    m_stats = PerformanceStats();
    m_stats.minProcessingTime = UINT64_MAX;
}

void Worker::start() {
    qCDebug(lcApp) << "[DEBUG] Worker::start called for thread:" << QThread::currentThread()->objectName();

    State currentState = m_state.load();
    if ( currentState != State::Stopped ) {
        return;
    }

    setState(State::Starting);
    m_stopRequested.store(false);
    m_pauseRequested.store(false);

    // 重置性能统计
    resetPerformanceStats();

    // 关键改动：改为通过单次定时器在事件循环启动后调度 doStart
    // 原因：QThread::started 信号在事件循环启动之前发出，若在此直接调用 doStart/workLoop，
    //       该线程将没有事件循环，BlockingQueuedConnection/QueuedConnection 将无法投递到该线程，
    //       导致诸如 ThreadManager::stopThread(waitForFinish=true) 阻塞并引发测试超时。
    // 方案：使用 QTimer::singleShot(0, this, ...) 将 doStart 投递到 worker 所属线程的事件队列，
    //       等事件循环启动后再执行。workLoop 内部通过 QCoreApplication::processEvents() 主动处理事件，
    //       确保 stop/pause/resume 等跨线程调用能够被及时响应。
    qCDebug(lcApp) << "[DEBUG] Scheduling doStart after event loop starts for thread:" << QThread::currentThread()->objectName();
    QTimer::singleShot(0, this, [this]() {
        qCDebug(lcApp) << "[DEBUG] doStart executing in thread:" << QThread::currentThread()->objectName();
        doStart();
        qCDebug(lcApp) << "[DEBUG] doStart returned in thread:" << QThread::currentThread()->objectName();
    });
}

void Worker::stop(bool waitForFinish) {
    State currentState = m_state.load();
    if ( currentState == State::Stopped || currentState == State::Stopping ) {
        return;
    }

    qCDebug(lcApp) << "Stopping worker:" << m_name << "waitForFinish:" << (waitForFinish ? "true" : "false");

    m_waitForFinish = waitForFinish;
    m_stopRequested.store(true);
    setState(State::Stopping);

    // 唤醒可能在暂停状态等待的线程
    m_pauseCondition.wakeAll();

    // 根据waitForFinish调整强制停止超时时间
    int forceStopTimeout = waitForFinish ? 2000 : 500;

    // 启动强制停止定时器。不能使用 QTimer::singleShot(ms, this, ...) 重载——
    // Worker::stop() 由主线程调用，而 this (Worker) 活在 Worker 线程。
    // 该重载内部会 setParent(this)，将 QTimer 设为跨线程子对象，违反 Qt 规则
    // 并可能触发 QObject::setParent 断言失败或访问违例 (0xC0000005)。
    // 改为无 context 重载 + QPointer 守卫，回调通过 invokeMethod 调度避开跨线程 parent。
    QPointer<Worker> selfGuard(this);
    QTimer::singleShot(forceStopTimeout, [selfGuard, forceStopTimeout]() {
        if ( !selfGuard ) return;
        // 在 Worker 所属线程中检查状态并执行 doStop。
        // invokeMethod 使用 AutoConnection：若回调线程与 Worker 同线程
        // 则同步执行，否则 Queued 投递到 Worker 线程。
        // 在内层 lambda 显式捕获 w 和 forceStopTimeout（外层 lambda 的成员）
        Worker* w = selfGuard.data();
        QMetaObject::invokeMethod(w, [w, forceStopTimeout]() {
            if ( w && w->m_state.load() == State::Stopping ) {
                qCDebug(lcCoreThreading) << "Worker强制停止（超时" << forceStopTimeout << "ms）：" << w->m_name;
                w->doStop();
            }
        }, Qt::AutoConnection);
    });
}

void Worker::pause() {
    if ( m_stopRequested.load() ) {
        qCDebug(lcCoreThreading) << "Worker::pause() - stop already requested for" << m_name;
        return;
    }

    bool alreadyRequested = m_pauseRequested.exchange(true);
    if ( alreadyRequested ) {
        qCDebug(lcCoreThreading) << "Worker::pause() - pause already pending for" << m_name;
        return;
    }

    qCDebug(lcCoreThreading) << "Worker::pause() - pause requested, state:"
        << static_cast<int>(m_state.load());
}

void Worker::resume() {
    bool hadRequest = m_pauseRequested.exchange(false);
    if ( !hadRequest && m_state.load() != State::Paused ) {
        qCDebug(lcCoreThreading) << "Worker::resume() - nothing to resume for" << m_name;
        return;
    }

    m_pauseCondition.wakeAll();
    qCDebug(lcCoreThreading) << "Worker::resume() - wake issued for" << m_name;
}

void Worker::setState(State newState) {
    State oldState = m_state.exchange(newState);
    if ( oldState != newState ) {
        emit stateChanged(newState, oldState);
    }
}

bool Worker::shouldStop() const {
    // 同时响应线程中断请求，以提升停止的灵敏度
    if ( m_stopRequested.load() ) {
        return true;
    }
    QThread* t = QThread::currentThread();
    return t && t->isInterruptionRequested();
}

void Worker::waitIfPaused() {
    // 如果收到暂停请求且未停止，则进入可中断等待
    if ( m_pauseRequested.load() && !m_stopRequested.load() ) {
        // 切换到Paused状态并发射paused信号（在工作线程内发射，避免事件循环阻塞）
        if ( m_state.load() != State::Paused ) {
            setState(State::Paused);
            qCDebug(lcCoreThreading) << "Worker" << m_name << "entering paused state, emitting paused signal";
            emit paused();
        }
        QMutexLocker locker(&m_pauseMutex);
        qCDebug(lcCoreThreading) << "Worker" << m_name << "waiting in paused state";
        while ( m_pauseRequested.load() && !m_stopRequested.load() ) {
            // 处理Qt事件，保证其它槽函数能被执行
            QCoreApplication::processEvents();
            // 带超时等待，周期性检查退出条件
            m_pauseCondition.wait(&m_pauseMutex, 50);
        }
        qCDebug(lcCoreThreading) << "Worker" << m_name << "exited pause wait loop, pauseRequested:"
            << m_pauseRequested.load() << "stopRequested:" << m_stopRequested.load()
            << "state:" << static_cast<int>(m_state.load());
        // 从暂停恢复：如果未停止，则切换回Running并发射resumed信号
        if ( !m_stopRequested.load() && m_state.load() == State::Paused ) {
            setState(State::Running);
            qCDebug(lcCoreThreading) << "Worker" << m_name << "emitting resumed signal";
            emit resumed();
            qCDebug(lcCoreThreading) << "Worker" << m_name << "resumed signal emitted";
        } else {
            qCDebug(lcCoreThreading) << "Worker" << m_name << "NOT emitting resumed signal - stopRequested:"
                << m_stopRequested.load() << "state:" << static_cast<int>(m_state.load());
        }
    }
}

void Worker::startPerformanceTiming() {
    m_processingTimer.start();
}

void Worker::endPerformanceTiming() {
    if ( m_processingTimer.isValid() ) {
        quint64 elapsed = m_processingTimer.elapsed();
        updatePerformanceStats(elapsed);
    }
}

void Worker::emitError(const QString& error) {
    qCDebug(lcCoreThreading) << "Worker error in" << m_name << ":" << error;
    emit errorOccurred(RdError(ErrorCode::Unknown, error, m_name));
}

void Worker::setDidWork(bool didWork) {
    m_adaptiveSleepEnabled.store(true);
    m_lastDidWork.store(didWork);
}

bool Worker::initialize() {
    // 默认实现：什么都不做，返回成功
    return true;
}

void Worker::cleanup() {
    // 默认实现：什么都不做
}

void Worker::workLoop() {
    qCDebug(lcCoreThreading) << "Worker" << m_name << "开始工作循环";

    try {
        while ( !shouldStop() ) {
            // Process pending events once per iteration so that QueuedConnection
            // invocations (e.g. startCapturing) and timers are delivered promptly.
            QCoreApplication::processEvents();

            waitIfPaused();

            if ( shouldStop() ) {
                break;
            }

            startPerformanceTiming();
            processTask();
            endPerformanceTiming();

            // Adaptive sleep: subclasses that call setDidWork() opt into
            // skipping the idle sleep when they actually processed data.
            // This eliminates the 1ms overhead in hot paths (e.g. screen
            // data sending) while still yielding CPU when idle.
            if ( m_adaptiveSleepEnabled.load() ) {
                if ( !m_lastDidWork.load() ) {
                    QThread::msleep(1);  // Idle — yield CPU
                }
                // Reset for next iteration (default to idle if subclass doesn't call setDidWork)
                m_lastDidWork.store(false);
            } else {
                // Legacy behavior: always sleep 1ms (backward compatible)
                QThread::msleep(1);
            }
        }
    } catch ( const std::exception& e ) {
        emitError(QString("Exception in work loop: %1").arg(e.what()));
    } catch ( ... ) {
        emitError("Unknown exception in work loop");
    }

    qCDebug(lcCoreThreading) << "Worker" << m_name << "工作循环结束";
}

void Worker::doStart() {
    try {
        // 初始化
        if ( !initialize() ) {
            emitError("Failed to initialize worker");
            setState(State::Stopped);
            return;
        }

        setState(State::Running);
        m_uptimeTimer.start();
        emit started();

        // 开始工作循环
        workLoop();

        // 工作循环正常退出后，执行收尾并发射stopped信号
        // 说明：此前仅在强制停止超时定时器中发射stopped，
        // 当正常退出时未发射，导致上层ThreadManager无法收到threadStopped，
        // 使测试用例（如test_stopThread/test_threadSafety）可能等待超时。
        // 这里统一在正常退出路径调用doStop()，以保证行为一致。
        doStop();

    } catch ( const std::exception& e ) {
        emitError(QString("Exception during start: %1").arg(e.what()));
    } catch ( ... ) {
        emitError("Unknown exception during start");
    }
}

void Worker::doStop() {
    // 兼容保留：若有需要在此收尾可加入
    cleanup();
    setState(State::Stopped);
    emit stopped();

    // 确保事件循环退出：
    // 说明：当工作循环正常结束或强制停止触发时，统一在所属线程中请求退出事件循环，
    // 避免线程保持运行导致 QThread 在销毁时仍在运行的错误。
    QThread* workerThread = this->thread();
    if ( workerThread && workerThread->isRunning() ) {
        qCDebug(lcApp) << "[DEBUG] Worker::doStop() requesting thread quit for:" << m_name;
        workerThread->quit();
    }
}

void Worker::updatePerformanceStats(quint64 processingTime) {
    QMutexLocker locker(&m_statsMutex);
    m_stats.totalProcessedItems++;
    m_stats.totalProcessingTime += processingTime;
    if ( processingTime > m_stats.maxProcessingTime ) {
        m_stats.maxProcessingTime = processingTime;
    }
    if ( processingTime < m_stats.minProcessingTime ) {
        m_stats.minProcessingTime = processingTime;
    }
    if ( m_stats.totalProcessedItems > 0 ) {
        m_stats.averageProcessingTime = m_stats.totalProcessingTime / m_stats.totalProcessedItems;
    }
}