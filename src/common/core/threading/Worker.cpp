#include "Worker.h"
#include "../logging/LoggingCategories.h"
#include <QtCore/QThread>
#include <QtCore/QMutexLocker>
#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtCore/QCoreApplication>

Worker::Worker(QObject *parent)
    : QObject(parent)
    , m_state(State::Stopped)
    , m_stopRequested(false)
    , m_pauseRequested(false)
    , m_name("Worker")
    , m_waitForFinish(true)
{
    // 初始化性能统计
    resetPerformanceStats();
}

Worker::~Worker()
{
    // 确保工作线程已停止
    if (m_state.load() != State::Stopped) {
        stop(false); // 不等待完成，强制停止
        
        // 等待状态变为Stopped，最多等待3秒
        int timeout = 0;
        while (m_state.load() != State::Stopped && timeout < 300) {
            QThread::msleep(10);
            timeout++;
        }
        
        if (m_state.load() != State::Stopped) {
            qCDebug(lcThreading) << "Worker destructor: Worker did not stop within timeout";
        }
    }
}

Worker::State Worker::state() const
{
    return m_state.load();
}

bool Worker::isRunning() const
{
    State currentState = m_state.load();
    return currentState == State::Running || currentState == State::Starting;
}

bool Worker::isPaused() const
{
    return m_state.load() == State::Paused;
}

bool Worker::isStopped() const
{
    State currentState = m_state.load();
    return currentState == State::Stopped || currentState == State::Stopping;
}

QString Worker::name() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_name;
}

void Worker::setName(const QString& name)
{
    QMutexLocker locker(&m_stateMutex);
    m_name = name;
}

Worker::PerformanceStats Worker::getPerformanceStats() const
{
    QMutexLocker locker(&m_statsMutex);
    PerformanceStats stats = m_stats;
    
    // 更新运行时间
    if (m_uptimeTimer.isValid()) {
        stats.uptime = m_uptimeTimer.elapsed();
    }
    
    // 计算每秒处理项目数
    if (stats.uptime > 0) {
        stats.itemsPerSecond = (stats.totalProcessedItems * 1000.0) / stats.uptime;
    }
    
    return stats;
}

void Worker::resetPerformanceStats()
{
    QMutexLocker locker(&m_statsMutex);
    m_stats = PerformanceStats();
    m_stats.minProcessingTime = UINT64_MAX;
}

void Worker::start()
{
    qCDebug(lcApp, "[DEBUG] Worker::start called for thread: %s", qPrintable(QThread::currentThread()->objectName()));
    
    State currentState = m_state.load();
    if (currentState != State::Stopped) {
        return;
    }
    
    setState(State::Starting);
    m_stopRequested.store(false);
    m_pauseRequested.store(false);
    
    // 重置性能统计
    resetPerformanceStats();
    
    qCDebug(lcApp, "[DEBUG] About to call doStart for thread: %s", qPrintable(QThread::currentThread()->objectName()));
    
    // 直接调用doStart，避免QueuedConnection在新线程中的问题
    doStart();
    
    qCDebug(lcApp, "[DEBUG] doStart completed for thread: %s", qPrintable(QThread::currentThread()->objectName()));
}

void Worker::stop(bool waitForFinish)
{
    State currentState = m_state.load();
    if (currentState == State::Stopped || currentState == State::Stopping) {
        return;
    }
    
    qCDebug(lcApp, "Stopping worker: %s waitForFinish: %s", qPrintable(m_name), waitForFinish ? "true" : "false");
    
    m_waitForFinish = waitForFinish;
    m_stopRequested.store(true);
    setState(State::Stopping);
    
    // 唤醒可能在暂停状态等待的线程
    m_pauseCondition.wakeAll();
    
    // 根据waitForFinish调整强制停止超时时间
    int forceStopTimeout = waitForFinish ? 2000 : 500; // 同步停止3秒，异步停止1秒
    
    // 启动强制停止定时器，直接在主线程中创建
    QTimer::singleShot(forceStopTimeout, QCoreApplication::instance(), [this, forceStopTimeout]() {
        if (m_state.load() == State::Stopping) {
            // 如果超时后仍然是Stopping状态，强制设置为Stopped
            qCDebug(lcThreading) << "Worker强制停止（超时" << forceStopTimeout << "ms）：" << m_name;
            setState(State::Stopped);
            emit stopped();
        }
    });
}

void Worker::pause()
{
    State currentState = m_state.load();
    if (currentState != State::Running) {
        qCDebug(lcThreading) << "Worker::pause() - Worker is not running:" << static_cast<int>(currentState);
        return;
    }
    
    // 仅设置标志，不直接改变状态与发信号，交由waitIfPaused统一处理，避免状态竞争
    m_pauseRequested.store(true);
}

void Worker::resume()
{
    State currentState = m_state.load();
    if (currentState != State::Paused && !m_pauseRequested.load()) {
        qCDebug(lcThreading) << "Worker::resume() - Worker is not paused:" << static_cast<int>(currentState);
        return;
    }
    
    // 清除暂停请求并唤醒
    m_pauseRequested.store(false);
    m_pauseCondition.wakeAll();
}

void Worker::requestPause()
{
    // 无条件设置请求，线程安全，供外部在任意线程调用
    m_pauseRequested.store(true);
}

void Worker::requestResume()
{
    m_pauseRequested.store(false);
    m_pauseCondition.wakeAll();
}

void Worker::setState(State newState)
{
    State oldState = m_state.exchange(newState);
    if (oldState != newState) {
        emit stateChanged(newState, oldState);
    }
}

bool Worker::shouldStop() const
{
    return m_stopRequested.load();
}

void Worker::waitIfPaused()
{
    // 如果收到暂停请求且未停止，则进入可中断等待
    if (m_pauseRequested.load() && !m_stopRequested.load()) {
        // 切换到Paused状态并发射paused信号（在工作线程内发射，避免事件循环阻塞）
        if (m_state.load() != State::Paused) {
            setState(State::Paused);
            emit paused();
        }
        QMutexLocker locker(&m_pauseMutex);
        while (m_pauseRequested.load() && !m_stopRequested.load()) {
            // 处理Qt事件，保证其它槽函数能被执行
            QCoreApplication::processEvents();
            // 带超时等待，周期性检查退出条件
            m_pauseCondition.wait(&m_pauseMutex, 50);
        }
        // 从暂停恢复：如果未停止，则切换回Running并发射resumed信号
        if (!m_stopRequested.load() && m_state.load() == State::Paused) {
            setState(State::Running);
            emit resumed();
        }
    }
}

void Worker::startPerformanceTiming()
{
    m_processingTimer.start();
}

void Worker::endPerformanceTiming()
{
    if (m_processingTimer.isValid()) {
        quint64 elapsed = m_processingTimer.elapsed();
        updatePerformanceStats(elapsed);
    }
}

void Worker::emitError(const QString& error)
{
    qCDebug(lcThreading) << "Worker error in" << m_name << ":" << error;
    emit errorOccurred(error);
}

bool Worker::initialize()
{
    // 默认实现：什么都不做，返回成功
    return true;
}

void Worker::cleanup()
{
    // 默认实现：什么都不做
}

void Worker::workLoop() {
    qCDebug(lcThreading) << "Worker" << m_name << "开始工作循环";
    
    try {
        while (!shouldStop()) {
            // 关键改动：在循环顶部处理事件，确保QueuedConnection/计时器能够在工作线程被占用时仍然得到投递与执行
            // 这样可以保证例如 ScreenCaptureWorker::startCapturing 通过 QMetaObject::invokeMethod(Qt::QueuedConnection)
            // 能够被及时调度执行，避免因workLoop长时间占用而导致事件队列饿死，进而引发测试超时（如test_syncCapture）
            QCoreApplication::processEvents();

            waitIfPaused();
            
            if (shouldStop()) {
                break;
            }
            
            startPerformanceTiming();
            
            // 在processTask前再次处理事件，尽量降低事件投递的延迟
            QCoreApplication::processEvents();
            
            // 在processTask前检查停止状态
            if (shouldStop()) {
                break;
            }
            
            processTask();
            
            // 在processTask后立即检查停止状态
            if (shouldStop()) {
                break;
            }
            
            endPerformanceTiming();
            
            // 再次处理事件，确保刚刚在processTask过程中产生的Queued信号能够被及时投递
            QCoreApplication::processEvents();
            
            // 最后检查停止状态
            if (shouldStop()) {
                break;
            }
            
            // 短暂休眠以避免紧密循环，但保持对停止请求的响应性
            QThread::msleep(1);
        }
    } catch (const std::exception& e) {
        emitError(QString("Exception in work loop: %1").arg(e.what()));
    } catch (...) {
        emitError("Unknown exception in work loop");
    }
    
    qCDebug(lcThreading) << "Worker" << m_name << "工作循环结束";
}

void Worker::doStart()
{
    try {
        // 初始化
        if (!initialize()) {
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
        
    } catch (const std::exception& e) {
        emitError(QString("Exception during start: %1").arg(e.what()));
    } catch (...) {
        emitError("Unknown exception during start");
    }
}

void Worker::doStop()
{
    // 兼容保留：若有需要在此收尾可加入
    cleanup();
    setState(State::Stopped);
    emit stopped();
}

void Worker::updatePerformanceStats(quint64 processingTime)
{
    QMutexLocker locker(&m_statsMutex);
    m_stats.totalProcessedItems++;
    m_stats.totalProcessingTime += processingTime;
    if (processingTime > m_stats.maxProcessingTime) {
        m_stats.maxProcessingTime = processingTime;
    }
    if (processingTime < m_stats.minProcessingTime) {
        m_stats.minProcessingTime = processingTime;
    }
    if (m_stats.totalProcessedItems > 0) {
        m_stats.averageProcessingTime = m_stats.totalProcessingTime / m_stats.totalProcessedItems;
    }
}