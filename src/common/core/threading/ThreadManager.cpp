#include "ThreadManager.h"
#include "Worker.h"
#include <QtCore/QMutexLocker>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>
#include <QtCore/QLoggingCategory> // 引入QLoggingCategory以使用分类日志
#include "../logging/LoggingCategories.h" // 引入日志分类声明，使用lcThreading进行分类日志输出

// 静态成员初始化
ThreadManager* ThreadManager::s_instance = nullptr;

ThreadManager* ThreadManager::instance()
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    
    if (!s_instance) {
        s_instance = new ThreadManager();
    }
    
    return s_instance;
}

ThreadManager::ThreadManager(QObject *parent)
    : QObject(parent)
    , m_monitoringTimer(new QTimer(this))
    , m_monitoringInterval(5000) // 默认5秒
    , m_monitoringEnabled(true)
{
    // 设置监控定时器
    m_monitoringTimer->setSingleShot(false);
    connect(m_monitoringTimer, &QTimer::timeout, this, &ThreadManager::onMonitoringTimer);
    
    if (m_monitoringEnabled) {
        m_monitoringTimer->start(m_monitoringInterval);
    }
    
    qCDebug(lcThreading) << "ThreadManager initialized"; // 使用分类debug日志替换qDebug，输出初始化信息
}

ThreadManager::~ThreadManager()
{
    qCDebug(lcThreading) << "ThreadManager destroying..."; // 析构开始
    
    // 停止监控
    if (m_monitoringTimer->isActive()) {
        m_monitoringTimer->stop();
    }
    
    // 销毁所有线程
    destroyAllThreads();
    
    qCDebug(lcThreading) << "ThreadManager destroyed"; // 析构结束
}

bool ThreadManager::createThread(const QString& name, 
                                std::unique_ptr<Worker> worker,
                                bool autoStart,
                                bool autoRestart,
                                int maxRestarts)
{
    if (name.isEmpty() || !worker) {
        qCDebug(lcThreading) << "ThreadManager::createThread() - Invalid parameters";
        return false;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // 检查名称是否已存在
    if (m_threads.contains(name)) {
        qCDebug(lcThreading) << "ThreadManager::createThread() - Thread name already exists:" << name;
        return false;
    }
    
    // 创建线程信息
    ThreadInfo* threadInfo = new ThreadInfo();
    threadInfo->name = name;
    threadInfo->thread = new QThread();
    threadInfo->worker = worker.release();
    threadInfo->createdTime = QDateTime::currentDateTime();
    threadInfo->autoRestart = autoRestart;
    threadInfo->restartCount = 0;
    threadInfo->maxRestarts = maxRestarts;
    
    // 设置Worker名称
    threadInfo->worker->setName(name);
    
    // 将Worker移动到线程中
    threadInfo->worker->moveToThread(threadInfo->thread);
    
    // 连接Worker信号（在移动到线程之后）
    connectWorkerSignals(threadInfo->worker);
        
    // 连接线程信号
    connect(threadInfo->thread, &QThread::started,
            threadInfo->worker, &Worker::start);
    connect(threadInfo->thread, &QThread::finished,
            threadInfo->worker, [worker = threadInfo->worker]() {
        worker->stop();
    });
    
    qCDebug(lcThreading) << "ThreadManager::createThread() - Worker signals connected for:" << name;
    
    // 存储线程信息
    m_threads[name] = threadInfo;
    
    qCDebug(lcThreading) << "Thread created:" << name;
    emit threadCreated(name);
    
    // 自动启动
    if (autoStart) {
        locker.unlock();
        startThread(name);
    }
    
    return true;
}



// 新增：查询线程是否处于运行状态
bool ThreadManager::isThreadRunning(const QString& name) const
{
    QMutexLocker locker(&m_mutex);
    const ThreadInfo* info = findThreadInfo(name);
    if (!info || !info->thread) {
        return false;
    }
    
    bool threadRunning = info->thread->isRunning();
    if (!threadRunning) {
        return false;
    }
    
    // 结合Worker状态一起判断更加稳健（暂停也视为运行态）
    if (!info->worker) {
        return threadRunning; // 极端情况下没有worker，仅依据线程运行状态
    }
    Worker::State st = info->worker->state();
    switch (st) {
        case Worker::State::Running:
        case Worker::State::Starting:
        case Worker::State::Paused:
            return true;
        case Worker::State::Stopping:
        case Worker::State::Stopped:
        default:
            return false;
    }
}
bool ThreadManager::startThread(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    
    ThreadInfo* info = findThreadInfo(name);
    if (!info) {
        qCDebug(lcThreading) << "ThreadManager::startThread() - Thread not found:" << name;
        return false;
    }
    
    if (info->thread->isRunning()) {
        qCDebug(lcThreading) << "ThreadManager::startThread() - Thread already running:" << name; // 已在运行，使用warning级别
        return false;
    }
    
    info->startedTime = QDateTime::currentDateTime();
    info->thread->start();
    
    qCDebug(lcThreading) << "Thread started:" << name; // 线程已启动
    return true;
}

bool ThreadManager::stopThread(const QString& name, bool waitForFinish)
{
    QMutexLocker locker(&m_mutex);
    
    ThreadInfo* info = findThreadInfo(name);
    if (!info) {
        qCDebug(lcThreading) << "ThreadManager::stopThread() - Thread not found:" << name; // 未找到线程，警告
        return false;
    }
    
    if (!info->thread->isRunning()) {
        qCDebug(lcThreading) << "ThreadManager::stopThread() - Thread not running:" << name; // 线程未运行，调试
        return true;
    }
    
    qCDebug(lcThreading) << "Stopping thread:" << name << "waitForFinish:" << waitForFinish;
    
    // 停止Worker - 使用DirectConnection立即执行
    QMetaObject::invokeMethod(info->worker, "stop", 
                             Qt::DirectConnection,
                             Q_ARG(bool, waitForFinish));
    
    // 等待线程结束
    locker.unlock();
    
    if (waitForFinish) {
        // 首先尝试优雅停止
        info->thread->quit();
        
        // 第一阶段：等待500毫秒进行优雅停止
        if (info->thread->wait(500)) {
            qCDebug(lcThreading) << "Thread gracefully stopped:" << name; // 优雅停止成功，使用debug级别
        } else {
            qCDebug(lcThreading) << "Thread" << name << "did not stop gracefully, trying terminate..."; // 优雅停止失败，提示即将强制终止
            
            // 第二阶段：强制终止并等待500毫秒
            info->thread->terminate();
            if (info->thread->wait(500)) {
                qCDebug(lcThreading) << "Thread forcefully terminated:" << name; // 强制终止成功，使用warning级别
            } else {
                qCDebug(lcThreading) << "未在超时内优雅停止，作为兜底调用 terminate:" << name;
                info->thread->terminate();
                info->thread->wait(300);
            }
        }
    } else {
        // 异步停止，不立即调用quit()，让Worker自己处理停止
        // info->thread->quit(); // 注释掉，让Worker的workLoop自然结束
    }
    
    qCDebug(lcThreading) << "Thread stopped:" << name;
    return true;
}

bool ThreadManager::pauseThread(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    
    ThreadInfo* info = findThreadInfo(name);
    if (!info) {
        qCDebug(lcThreading) << "ThreadManager::pauseThread() - Thread not found:" << name;
        return false;
    }
    
    if (!info->thread->isRunning()) {
        qCDebug(lcThreading) << "ThreadManager::pauseThread() - Thread not running:" << name;
        return false;
    }
    
    // 关键点：在调用 Worker 的请求接口前释放管理器互斥锁，
    // 避免 Worker 立即发射的 paused 信号在 onWorkerPaused 内部再次获取 m_mutex 时发生竞争，
    // 造成不必要的等待甚至超时（影响测试稳定性）。
    Worker* worker = info->worker; // 记录指针以便在解锁后使用
    locker.unlock();
    
    if (worker) {
        // 直接调用线程安全的请求接口，避免事件循环阻塞导致 QueuedConnection 无法触发
        worker->requestPause();
    } else {
        qCDebug(lcThreading) << "ThreadManager::pauseThread() - Worker is null for:" << name; // 使用分类debug日志
        return false;
    }
    
    qCDebug(lcThreading) << "Thread paused:" << name; // 统一为debug级别
    return true;
}

bool ThreadManager::resumeThread(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    
    ThreadInfo* info = findThreadInfo(name);
    if (!info) {
        qCDebug(lcThreading) << "ThreadManager::resumeThread() - Thread not found:" << name; // 统一为debug
        return false;
    }
    
    if (!info->thread->isRunning()) {
        qCDebug(lcThreading) << "ThreadManager::resumeThread() - Thread not running:" << name; // 统一为debug
        return false;
    }
    
    // 同上：释放互斥锁，避免 Worker 发射 resumed 信号后，ThreadManager 槽函数 getThreadNameByWorker()
    // 试图获取同一把 m_mutex 而造成的竞争与延迟（测试中对 200ms 的时序较为敏感）。
    Worker* worker = info->worker;
    locker.unlock();

    if (worker) {
        // 直接调用线程安全的请求接口进行恢复
        worker->requestResume();
    } else {
        qCDebug(lcThreading) << "ThreadManager::resumeThread() - Worker is null for:" << name; // 统一为debug
        return false;
    }
    
    qCDebug(lcThreading) << "Thread resumed:" << name;
    return true;
}

bool ThreadManager::restartThread(const QString& name)
{
    if (!stopThread(name, true)) {
        return false;
    }
    
    // 等待一小段时间确保线程完全停止
    QThread::msleep(100);
    
    return startThread(name);
}

bool ThreadManager::destroyThread(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    
    ThreadInfo* info = findThreadInfo(name);
    if (!info) {
        qCDebug(lcThreading) << "ThreadManager::destroyThread() - Thread not found:" << name;
        return false;
    }
    
    qCDebug(lcThreading) << "Destroying thread:" << name;
    
    // 先断开Worker信号，防止在清理过程中收到信号
    disconnectWorkerSignals(info->worker);
    
    // 停止线程，使用同步等待确保完全停止
    if (info->thread->isRunning()) {
        // 设置停止标志，让workLoop自然退出
        QMetaObject::invokeMethod(info->worker, "stop", 
                                 Qt::DirectConnection,
                                 Q_ARG(bool, false)); // 不等待完成，让线程自然退出
        
        // 等待线程自然退出（workLoop应该已经因为stop()而退出）
        if (!info->thread->wait(500)) {
            qCDebug(lcThreading) << "Thread did not stop gracefully, forcing termination:" << name; // 统一为debug
            info->thread->terminate();
            info->thread->wait(300);
        }
    }
    
    // 处理任何待处理的事件
    QCoreApplication::processEvents();
    
    // 手动删除ThreadInfo对象（析构函数会清理worker和thread）
    delete info;
    
    // 移除线程信息
    m_threads.remove(name);
    
    qCDebug(lcThreading) << "Thread destroyed:" << name;
    emit threadDestroyed(name);
    
    return true;
}

void ThreadManager::startAllThreads()
{
    QMutexLocker locker(&m_mutex);
    
    for (auto it = m_threads.begin(); it != m_threads.end(); ++it) {
        const QString& name = it.key();
        locker.unlock();
        startThread(name);
        locker.relock();
    }
    
    qCDebug(lcThreading) << "All threads started";
}

void ThreadManager::stopAllThreads(bool waitForFinish)
{
    QMutexLocker locker(&m_mutex);
    
    QStringList threadNames = m_threads.keys();
    int totalThreads = threadNames.size();
    
    if (totalThreads == 0) {
        qCDebug(lcThreading) << "No threads to stop";
        return;
    }
    
    qCDebug(lcThreading) << "Stopping" << totalThreads << "threads, waitForFinish:" << waitForFinish;
    
    if (waitForFinish) {
        // 同步停止：逐个停止并等待
        int stoppedCount = 0;
        for (const QString& name : threadNames) {
            locker.unlock();
            bool success = stopThread(name, true);
            locker.relock();
            
            if (success) {
                stoppedCount++;
                qCDebug(lcThreading) << "Progress:" << stoppedCount << "/" << totalThreads << "threads stopped";
            } else {
                qCDebug(lcThreading) << "Failed to stop thread:" << name;
            }
        }
        
        if (stoppedCount == totalThreads) {
            qCDebug(lcThreading) << "All" << totalThreads << "threads stopped successfully";
        } else {
            qCDebug(lcThreading) << "Only" << stoppedCount << "out of" << totalThreads << "threads stopped successfully";
        }
    } else {
        // 异步停止：并行发送停止信号
        for (const QString& name : threadNames) {
            locker.unlock();
            stopThread(name, false);
            locker.relock();
        }
        qCDebug(lcThreading) << "Stop signals sent to all" << totalThreads << "threads";
    }
}

void ThreadManager::pauseAllThreads()
{
    QMutexLocker locker(&m_mutex);
    
    for (auto it = m_threads.begin(); it != m_threads.end(); ++it) {
        const QString& name = it.key();
        locker.unlock();
        pauseThread(name);
        locker.relock();
    }
    
    qCDebug(lcThreading) << "All threads paused";
}

void ThreadManager::resumeAllThreads()
{
    QMutexLocker locker(&m_mutex);
    
    for (auto it = m_threads.begin(); it != m_threads.end(); ++it) {
        const QString& name = it.key();
        locker.unlock();
        resumeThread(name);
        locker.relock();
    }
    
    qCDebug(lcThreading) << "All threads resumed";
}

void ThreadManager::destroyAllThreads()
{
    QStringList threadNames;
    QList<ThreadInfo*> threadsToDelete;
    
    // 第一步：获取所有线程名称并从映射中移除
    {
        QMutexLocker locker(&m_mutex);
        threadNames = m_threads.keys();
        
        if (threadNames.isEmpty()) {
            qCDebug(lcThreading) << "No threads to destroy";
            return;
        }
        
        qCDebug(lcThreading) << "Destroying" << threadNames.size() << "threads...";
        
        // 从映射中移除所有线程，防止其他操作访问
        for (const QString& name : threadNames) {
            ThreadInfo* info = m_threads.take(name);
            if (info) {
                threadsToDelete.append(info);
            }
        }
        
        // 确保映射已清空
        m_threads.clear();
    }
    
    // 第二步：在没有锁的情况下停止和清理线程
    int forcedCount = 0; // 统计实际执行强制终止的线程数量，用于更准确的汇总日志
    for (ThreadInfo* info : threadsToDelete) {
        if (info && info->thread) {
            qCDebug(lcThreading) << "Attempting graceful stop for thread:" << info->name; // 先尝试优雅停止，而非直接宣称强制终止
            if (info->worker) {
                disconnectWorkerSignals(info->worker);
                QMetaObject::invokeMethod(info->worker, "stop",
                                         Qt::DirectConnection,
                                         Q_ARG(bool, false));
            }
            if (info->thread->isRunning()) {
                if (!info->thread->wait(300)) {
                    qCDebug(lcThreading) << "Graceful stop timed out (300ms), forcing termination for thread:" << info->name; // 优雅停止超时后才进行强制终止，统一为debug
                    info->thread->terminate();
                    if (!info->thread->wait(200)) {
                        qCDebug(lcThreading) << "Thread" << info->name << "failed to terminate within secondary timeout (200ms)!"; // 二次等待仍失败，统一为debug
                    }
                    forcedCount++; // 记录一次强制终止
                }
            }
        }
    }
    
    // 第三步：处理任何待处理的事件
    QCoreApplication::processEvents();
    
    // 第四步：删除ThreadInfo对象
    for (ThreadInfo* info : threadsToDelete) {
        delete info;
    }
    
    qCDebug(lcThreading) << "All" << threadNames.size() << "threads destroyed; forced terminations:" << forcedCount;
}

bool ThreadManager::hasThread(const QString& name) const
{
    QMutexLocker locker(&m_mutex);
    return m_threads.contains(name);
}

const ThreadManager::ThreadInfo* ThreadManager::getThreadInfo(const QString& name) const
{
    QMutexLocker locker(&m_mutex);
    return findThreadInfo(name);
}

QStringList ThreadManager::getThreadNames() const
{
    QMutexLocker locker(&m_mutex);
    return m_threads.keys();
}

ThreadManager::ThreadStats ThreadManager::getThreadStats() const
{
    QMutexLocker locker(&m_mutex);
    
    ThreadStats stats;
    stats.totalThreads = m_threads.size();
    
    quint64 totalUptime = 0;
    int runningSamples = 0;
    
    for (auto it = m_threads.begin(); it != m_threads.end(); ++it) {
        const ThreadInfo* info = it.value();
        
        if (info->thread->isRunning()) {
            Worker::State state = info->worker->state();
            switch (state) {
                case Worker::State::Running:
                case Worker::State::Starting:
                    stats.runningThreads++;
                    break;
                case Worker::State::Paused:
                    stats.pausedThreads++;
                    break;
                case Worker::State::Stopped:
                case Worker::State::Stopping:
                    stats.stoppedThreads++;
                    break;
            }
            
            // 计算运行时间
            if (info->startedTime.isValid()) {
                quint64 uptime = info->startedTime.msecsTo(QDateTime::currentDateTime());
                totalUptime += uptime;
                runningSamples++;
            }
        } else {
            stats.stoppedThreads++;
        }
    }
    
    stats.totalUptime = totalUptime;
    if (runningSamples > 0) {
        stats.averageUptime = totalUptime / runningSamples;
    }
    
    return stats;
}

Worker* ThreadManager::getWorker(const QString& name) const
{
    QMutexLocker locker(&m_mutex);
    
    const ThreadInfo* info = findThreadInfo(name);
    return info ? info->worker : nullptr;
}

void ThreadManager::setMonitoringInterval(int intervalMs)
{
    QMutexLocker locker(&m_mutex);
    
    m_monitoringInterval = intervalMs;
    
    if (m_monitoringEnabled && m_monitoringTimer->isActive()) {
        m_monitoringTimer->stop();
        m_monitoringTimer->start(m_monitoringInterval);
    }
}

int ThreadManager::monitoringInterval() const
{
    QMutexLocker locker(&m_mutex);
    return m_monitoringInterval;
}

void ThreadManager::setMonitoringEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    
    m_monitoringEnabled = enabled;
    
    if (enabled && !m_monitoringTimer->isActive()) {
        m_monitoringTimer->start(m_monitoringInterval);
    } else if (!enabled && m_monitoringTimer->isActive()) {
        m_monitoringTimer->stop();
    }
}

bool ThreadManager::isMonitoringEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_monitoringEnabled;
}

void ThreadManager::onWorkerStarted()
{
    Worker* worker = qobject_cast<Worker*>(sender());
    if (!worker) return;
    
    QString name = getThreadNameByWorker(worker);
    if (!name.isEmpty()) {
        emit threadStarted(name);
    }
}

void ThreadManager::onWorkerStopped()
{
    qCDebug(lcThreading) << "ThreadManager::onWorkerStopped() called";
    
    QObject* senderObj = sender();
    if (!senderObj) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerStopped() - No sender object, signal likely from deleted worker";
        return;
    }
    
    // 安全地检查sender是否仍然是有效的Worker对象
    Worker* worker = qobject_cast<Worker*>(senderObj);
    if (!worker) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerStopped() - Invalid sender, not a Worker object";
        return;
    }
    
    // 检查Worker对象是否仍然在我们的线程列表中
    QMutexLocker locker(&m_mutex);
    QString threadName;
    bool found = false;
    
    for (auto it = m_threads.begin(); it != m_threads.end(); ++it) {
        if (it.value()->worker == worker) {
            threadName = it.key();
            found = true;
            break;
        }
    }
    
    if (!found) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerStopped() - Worker not found in thread list, ignoring signal";
        return;
    }
    
    qCDebug(lcThreading) << "ThreadManager::onWorkerStopped() - threadStopped signal emitted for:" << threadName;
    emit threadStopped(threadName);
}

void ThreadManager::onWorkerPaused()
{
    qCDebug(lcThreading) << "ThreadManager::onWorkerPaused() called";
    Worker* worker = qobject_cast<Worker*>(sender());
    if (!worker) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerPaused() - No worker sender";
        return;
    }
    
    QString name = getThreadNameByWorker(worker);
    qCDebug(lcThreading) << "ThreadManager::onWorkerPaused() - Worker name:" << name;
    if (!name.isEmpty()) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerPaused() - Emitting threadPaused signal for:" << name;
        emit threadPaused(name);
        qCDebug(lcThreading) << "ThreadManager::onWorkerPaused() - threadPaused signal emitted";
    }
}

void ThreadManager::onWorkerResumed()
{
    Worker* worker = qobject_cast<Worker*>(sender());
    if (!worker) return;
    
    QString name = getThreadNameByWorker(worker);
    if (!name.isEmpty()) {
        emit threadResumed(name);
    }
}

void ThreadManager::onWorkerError(const QString& error)
{
    Worker* worker = qobject_cast<Worker*>(sender());
    if (!worker) return;
    
    QString name = getThreadNameByWorker(worker);
    if (!name.isEmpty()) {
        emit threadError(name, error);
    }
}

void ThreadManager::onMonitoringTimer()
{
    ThreadStats stats = getThreadStats();
    emit performanceStatsUpdated(stats);
}

ThreadManager::ThreadInfo* ThreadManager::findThreadInfo(const QString& name)
{
    auto it = m_threads.find(name);
    return (it != m_threads.end()) ? it.value() : nullptr;
}

const ThreadManager::ThreadInfo* ThreadManager::findThreadInfo(const QString& name) const
{
    auto it = m_threads.find(name);
    return (it != m_threads.end()) ? it.value() : nullptr;
}

QString ThreadManager::getThreadNameByWorker(Worker* worker) const
{
    QMutexLocker locker(&m_mutex);
    
    for (auto it = m_threads.begin(); it != m_threads.end(); ++it) {
        if (it.value()->worker == worker) {
            return it.key();
        }
    }
    
    return QString();
}

void ThreadManager::connectWorkerSignals(Worker* worker)
{
    qCDebug(lcThreading) << "ThreadManager::connectWorkerSignals() - Worker thread:" << worker->thread();
    qCDebug(lcThreading) << "ThreadManager::connectWorkerSignals() - ThreadManager thread:" << this->thread();
    qCDebug(lcThreading) << "ThreadManager::connectWorkerSignals() - Current thread:" << QThread::currentThread();
    
    // 尝试使用Qt::AutoConnection让Qt自动选择连接类型
    bool result1 = connect(worker, &Worker::started, this, &ThreadManager::onWorkerStarted, Qt::AutoConnection);
    bool result2 = connect(worker, &Worker::stopped, this, &ThreadManager::onWorkerStopped, Qt::AutoConnection);
    bool result3 = connect(worker, &Worker::paused, this, &ThreadManager::onWorkerPaused, Qt::AutoConnection);
    bool result4 = connect(worker, &Worker::resumed, this, &ThreadManager::onWorkerResumed, Qt::AutoConnection);
    bool result5 = connect(worker, &Worker::errorOccurred, this, &ThreadManager::onWorkerError, Qt::AutoConnection);
    
    qCDebug(lcThreading) << "ThreadManager::connectWorkerSignals() - Connected signals for worker:" << worker->name();
    qCDebug(lcThreading) << "ThreadManager::connectWorkerSignals() - Connection results:" << result1 << result2 << result3 << result4 << result5;
    
    if (!result2) {
        qCDebug(lcThreading) << "ThreadManager::connectWorkerSignals() - FAILED to connect stopped signal for worker:" << worker->name();
    }
    
    // 验证连接是否真的建立
    QMetaObject::Connection conn = connect(worker, &Worker::stopped, this, [worker]() {
        qCDebug(lcThreading) << "Lambda slot received stopped signal from:" << worker->name();
    }, Qt::AutoConnection);
    qCDebug(lcThreading) << "ThreadManager::connectWorkerSignals() - Lambda connection valid:" << (bool)conn;
}

void ThreadManager::disconnectWorkerSignals(Worker* worker)
{
    worker->disconnect(this);
}

void ThreadManager::tryAutoRestart(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    
    ThreadInfo* info = findThreadInfo(name);
    if (!info || !info->autoRestart) {
        return;
    }
    
    // 检查重启次数限制
    if (info->maxRestarts >= 0 && info->restartCount >= info->maxRestarts) {
        qCDebug(lcThreading) << "Thread" << name << "reached maximum restart limit:" << info->maxRestarts; // 统一为debug
        return;
    }
    
    info->restartCount++;
    
    qCDebug(lcThreading) << "Auto-restarting thread" << name << "(attempt" << info->restartCount << ")";
    
    // 延迟重启以避免快速循环
    QTimer::singleShot(1000, [this, name]() {
        if (hasThread(name)) {
            startThread(name);
            emit threadRestarted(name, findThreadInfo(name)->restartCount);
        }
    });
}