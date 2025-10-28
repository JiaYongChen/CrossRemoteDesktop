#include "ThreadManager.h"
#include "Worker.h"
#include <QtCore/QMutexLocker>
#include <QtCore/QPointer>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>
#include <QtCore/QLoggingCategory> // 引入QLoggingCategory以使用分类日志
#include "../logging/LoggingCategories.h" // 引入日志分类声明，使用lcThreading进行分类日志输出

// 静态成员初始化
ThreadManager* ThreadManager::s_instance = nullptr;

ThreadManager* ThreadManager::instance() {
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    if ( !s_instance ) {
        s_instance = new ThreadManager();
    }

    return s_instance;
}

ThreadManager::ThreadManager(QObject* parent)
    : QObject(parent)
    , m_monitoringTimer(new QTimer(this))
    , m_monitoringInterval(5000) // 默认5秒
    , m_monitoringEnabled(true) {
    // 设置监控定时器
    m_monitoringTimer->setSingleShot(false);
    connect(m_monitoringTimer, &QTimer::timeout, this, &ThreadManager::onMonitoringTimer);

    if ( m_monitoringEnabled ) {
        m_monitoringTimer->start(m_monitoringInterval);
    }

    qCDebug(lcThreading) << "ThreadManager initialized"; // 使用分类debug日志替换qDebug，输出初始化信息
}

ThreadManager::~ThreadManager() {
    qCDebug(lcThreading) << "ThreadManager destroying..."; // 析构开始

    // 停止监控
    if ( m_monitoringTimer->isActive() ) {
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
    int maxRestarts) {
    if ( name.isEmpty() || !worker ) {
        qCDebug(lcThreading) << "ThreadManager::createThread() - Invalid parameters";
        return false;
    }

    QMutexLocker locker(&m_mutex);

    // 检查名称是否已存在
    if ( m_threads.contains(name) ) {
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
    threadInfo->stopRequested = false;

    // 设置Worker名称
    threadInfo->worker->setName(name);

    // 将Worker移动到线程中
    threadInfo->worker->moveToThread(threadInfo->thread);

    // 连接Worker信号（在移动到线程之后）
    connectWorkerSignals(threadInfo->worker);

    // 连接线程信号
    connect(threadInfo->thread, &QThread::started,
        threadInfo->worker, &Worker::start);
    // 注意：不再将 QThread::finished 连接到 Worker::stop。
    // 线程事件循环退出时再次调用 stop 可能导致停止流程与定时器清理逻辑竞争，
    // 从而触发跨线程停止定时器的警告或造成停止信号未能按预期发射。
    // 停止流程统一由 ThreadManager::stopThread 与 Worker::doStop 管理。

    qCDebug(lcThreading) << "ThreadManager::createThread() - Worker signals connected for:" << name;

    // 存储线程信息
    m_threads[name] = threadInfo;

    qCDebug(lcThreading) << "Thread created:" << name;
    emit threadCreated(name);

    // 自动启动
    if ( autoStart ) {
        locker.unlock();
        startThread(name);
    }

    return true;
}

// 新增：查询线程是否处于运行状态
bool ThreadManager::isThreadRunning(const QString& name) const {
    QMutexLocker locker(&m_mutex);
    const ThreadInfo* info = findThreadInfo(name);
    if ( !info || !info->thread ) {
        return false;
    }

    bool threadRunning = info->thread->isRunning();
    if ( !threadRunning ) {
        return false;
    }

    // 结合Worker状态一起判断更加稳健（暂停也视为运行态）
    if ( !info->worker ) {
        return threadRunning; // 极端情况下没有worker，仅依据线程运行状态
    }
    Worker::State st = info->worker->state();
    switch ( st ) {
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
bool ThreadManager::startThread(const QString& name) {
    QMutexLocker locker(&m_mutex);

    ThreadInfo* info = findThreadInfo(name);
    if ( !info ) {
        qCDebug(lcThreading) << "ThreadManager::startThread() - Thread not found:" << name;
        return false;
    }

    if ( info->thread->isRunning() ) {
        qCDebug(lcThreading) << "ThreadManager::startThread() - Thread already running:" << name; // 已在运行，使用warning级别
        return false;
    }

    // 清除主动停止标记，新的启动表示进入正常运行期
    info->stopRequested = false;

    info->startedTime = QDateTime::currentDateTime();
    info->thread->start();

    qCDebug(lcThreading) << "Thread started:" << name; // 线程已启动
    return true;
}

bool ThreadManager::stopThread(const QString& name, bool waitForFinish) {
    QMutexLocker locker(&m_mutex);

    ThreadInfo* info = findThreadInfo(name);
    if ( !info ) {
        qCDebug(lcThreading) << "ThreadManager::stopThread() - Thread not found:" << name; // 未找到线程
        return false;
    }

    if ( !info->thread || !info->thread->isRunning() ) {
        qCDebug(lcThreading) << "ThreadManager::stopThread() - Thread not running:" << name; // 线程未运行
        return true;
    }

    // 标记为主动停止，避免自动重启
    info->stopRequested = true;
    qCDebug(lcThreading) << "Stopping thread:" << name << "waitForFinish:" << waitForFinish;

    Worker* worker = info->worker;
    QThread* thread = info->thread;
    locker.unlock();

    // 1) 请求 Worker 停止：先直接在当前线程设置停止标志与强制定时器，再异步排队到工作线程（第二次调用会立即返回）
    // 说明：Worker::stop 内部只设置原子标志并使用 singleShot 定时器触发 doStop，不进行跨线程的 QObject 操作，直接调用是安全的。
    // 直接调用可以确保在目标线程事件循环繁忙或被占用时也能及时改变状态，避免 isStopped() 长时间为 false 导致等待超时。
    worker->stop(waitForFinish);
    // 请求线程中断以加快workLoop中止（shouldStop也会检查isInterruptionRequested）
    if ( thread ) thread->requestInterruption();
    QMetaObject::invokeMethod(worker, "stop", Qt::QueuedConnection, Q_ARG(bool, waitForFinish));

    // 2) 轮询等待 Worker 发出 stopped 或线程退出，期间处理事件以推动跨线程信号投递
    const int maxWaitMs = waitForFinish ? 3500 : 1500;
    QElapsedTimer timer; timer.start();
    bool workerStopped = false;
    while ( timer.elapsed() < maxWaitMs ) {
        // 主动处理所有线程的事件，确保QueuedConnection可以被执行
        QCoreApplication::processEvents();
        // 检查 Worker 状态
        workerStopped = worker->isStopped();
        if ( workerStopped ) break;
        QThread::msleep(10);
    }
    if ( !workerStopped ) {
        qCWarning(lcThreading) << "Worker did not report stopped within" << maxWaitMs << "ms:" << name;
    }

    // 在删除对象前，异步请求 Worker 线程执行一次 cleanup（停止定时器/断开连接），
    // 避免在无事件循环或已停止时使用 BlockingQueuedConnection 导致潜在阻塞。
    QMetaObject::invokeMethod(worker, "callCleanup", Qt::QueuedConnection);

    // 3) 请求事件循环退出，并等待线程结束
    if ( thread->isRunning() ) {
        // 直接请求退出事件循环（跨线程安全）
        thread->quit();
        if ( !thread->wait(2000) ) {
            qCWarning(lcThreading) << "Thread did not quit within" << 2000 << "ms:" << name;
            if ( !thread->wait(500) ) {
                qCWarning(lcThreading) << "Thread still running after grace period:" << name;
            }
        }
    }

    if ( !thread->isRunning() ) {
        qCDebug(lcThreading) << "Thread stopped:" << name;
        return true;
    } else {
        qCWarning(lcThreading) << "ThreadManager::stopThread() - Thread still running when returning:" << name;
        // 明确返回失败，表明线程尚未停止，调用方应避免继续销毁对象
        return false;
    }
}

bool ThreadManager::pauseThread(const QString& name) {
    QMutexLocker locker(&m_mutex);

    ThreadInfo* info = findThreadInfo(name);
    if ( !info ) {
        qCDebug(lcThreading) << "ThreadManager::pauseThread() - Thread not found:" << name;
        return false;
    }

    if ( !info->thread->isRunning() ) {
        qCDebug(lcThreading) << "ThreadManager::pauseThread() - Thread not running:" << name;
        return false;
    }

    // 关键点：在调用 Worker 的请求接口前释放管理器互斥锁，
    // 避免 Worker 立即发射的 paused 信号在 onWorkerPaused 内部再次获取 m_mutex 时发生竞争，
    // 造成不必要的等待甚至超时（影响测试稳定性）。
    Worker* worker = info->worker; // 记录指针以便在解锁后使用
    locker.unlock();

    if ( worker ) {
        // 直接调用线程安全的请求接口，避免事件循环阻塞导致 QueuedConnection 无法触发
        worker->requestPause();
    } else {
        qCDebug(lcThreading) << "ThreadManager::pauseThread() - Worker is null for:" << name; // 使用分类debug日志
        return false;
    }

    qCDebug(lcThreading) << "Thread paused:" << name; // 统一为debug级别
    return true;
}

bool ThreadManager::resumeThread(const QString& name) {
    QMutexLocker locker(&m_mutex);

    ThreadInfo* info = findThreadInfo(name);
    if ( !info ) {
        qCDebug(lcThreading) << "ThreadManager::resumeThread() - Thread not found:" << name; // 统一为debug
        return false;
    }

    if ( !info->thread->isRunning() ) {
        qCDebug(lcThreading) << "ThreadManager::resumeThread() - Thread not running:" << name; // 统一为debug
        return false;
    }

    // 同上：释放互斥锁，避免 Worker 发射 resumed 信号后，ThreadManager 槽函数 getThreadNameByWorker()
    // 试图获取同一把 m_mutex 而造成的竞争与延迟（测试中对 200ms 的时序较为敏感）。
    Worker* worker = info->worker;
    locker.unlock();

    if ( worker ) {
        // 直接调用线程安全的请求接口进行恢复
        worker->requestResume();
    } else {
        qCDebug(lcThreading) << "ThreadManager::resumeThread() - Worker is null for:" << name; // 统一为debug
        return false;
    }

    qCDebug(lcThreading) << "Thread resumed:" << name;
    return true;
}

bool ThreadManager::restartThread(const QString& name) {
    if ( !stopThread(name, true) ) {
        return false;
    }

    // 等待一小段时间确保线程完全停止
    QThread::msleep(100);

    return startThread(name);
}

bool ThreadManager::destroyThread(const QString& name) {
    // 统一停线程逻辑：复用 stopThread，避免在 destroyThread 中重复等待造成额外超时
    // 此处不持有锁调用 stopThread，以减少锁竞争
    {
        QMutexLocker locker(&m_mutex);
        ThreadInfo* info = findThreadInfo(name);
        if ( !info ) {
            qCDebug(lcThreading) << "ThreadManager::destroyThread() - Thread not found:" << name;
            return false;
        }
        // 先断开Worker信号，防止在清理过程中收到信号
        disconnectWorkerSignals(info->worker);
    }

    qCDebug(lcThreading) << "Destroying thread:" << name;

    // 使用统一的停止流程，保证行为一致，且只等待一次
    bool stopped = stopThread(name, true);

    // 再次获取线程信息，依据实际运行状态决定是否删除
    QMutexLocker locker(&m_mutex);
    ThreadInfo* info = findThreadInfo(name);
    if ( !info ) {
        qCDebug(lcThreading) << "ThreadManager::destroyThread() - Thread info missing after stop:" << name;
        return false;
    }

    // 如果线程仍在运行，尝试升级为强制终止，但仍需在确认停止后才删除
    if ( info->thread && info->thread->isRunning() ) {
        qCWarning(lcThreading) << "Destroy aborted: thread still running, attempting graceful shutdown again for" << name;

        // 断开 worker 的信号，避免在清理过程中触发跨线程回调
        if ( info->worker ) {
            disconnectWorkerSignals(info->worker);
            // 再次尝试在 Worker 线程中异步触发清理与停止，避免跨线程停止定时器
            QMetaObject::invokeMethod(info->worker, "callCleanup", Qt::QueuedConnection);
            QMetaObject::invokeMethod(info->worker, "stop",
                Qt::QueuedConnection,
                Q_ARG(bool, false));
        }

        QThread* thread = info->thread;
        // 释放锁后再次请求事件循环退出并稍作等待，避免长时间持锁
        locker.unlock();
        if ( thread->isRunning() ) {
            thread->quit();
            // 给予更长的等待时间以便已排队的清理与停止在工作线程执行
            thread->wait(1500);
        }
        // 回到持锁状态
        locker.relock();

        // 若仍在运行，则中止销毁，避免删除仍在运行的QThread导致崩溃
        if ( info->thread && info->thread->isRunning() ) {
            qCWarning(lcThreading) << "Thread still running, destroy aborted:" << name;
            return false;
        }
    }

    // 到这里线程已确认停止，可以安全删除
    // 额外保险：在删除前执行一次清理，确保内部定时器/连接被断开，避免析构后仍有回调触发。
    // 注意：若此前发生过强制终止（terminate），Worker 仍然隶属到已终止的线程对象，
    // 在非所属线程直接访问其子对象（如QTimer）可能不安全。以确保清理和删除在活跃事件循环中进行，
    // 将 Worker 迁移回主线程后再执行清理与删除。
    // 清理工作已在 stopThread 中通过 QueuedConnection 调度到 worker 线程执行，
    // 此处在确认线程停止后，将 Worker 迁移回主线程并在同线程中执行一次清理，以确保删除安全。
    // 清理已通过 stopThread 中的 QueuedConnection 调度执行，此处不再跨线程调用或移动对象，避免 QObject::moveToThread 警告。
    // Worker 与 Thread 的删除统一由 ThreadInfo 析构处理，确保在确认线程停止后安全删除。

    ThreadInfo* infoToDelete = m_threads.take(name);
    // 处理任何待处理的事件
    locker.unlock();
    QCoreApplication::processEvents();
    if ( infoToDelete ) {
        delete infoToDelete; // 析构中会清理 worker 和 thread
    }

    qCDebug(lcThreading) << "Thread destroyed:" << name;
    emit threadDestroyed(name);
    return stopped;
}

void ThreadManager::startAllThreads() {
    QMutexLocker locker(&m_mutex);

    for ( auto it = m_threads.begin(); it != m_threads.end(); ++it ) {
        const QString& name = it.key();
        locker.unlock();
        startThread(name);
        locker.relock();
    }

    qCDebug(lcThreading) << "All threads started";
}

void ThreadManager::stopAllThreads(bool waitForFinish) {
    QMutexLocker locker(&m_mutex);

    QStringList threadNames = m_threads.keys();
    int totalThreads = threadNames.size();

    if ( totalThreads == 0 ) {
        qCDebug(lcThreading) << "No threads to stop";
        return;
    }

    qCDebug(lcThreading) << "Stopping" << totalThreads << "threads, waitForFinish:" << waitForFinish;

    if ( waitForFinish ) {
        // 同步停止：逐个停止并等待
        int stoppedCount = 0;
        for ( const QString& name : threadNames ) {
            locker.unlock();
            bool success = stopThread(name, true);
            locker.relock();

            if ( success ) {
                stoppedCount++;
                qCDebug(lcThreading) << "Progress:" << stoppedCount << "/" << totalThreads << "threads stopped";
            } else {
                qCDebug(lcThreading) << "Failed to stop thread:" << name;
            }
        }

        if ( stoppedCount == totalThreads ) {
            qCDebug(lcThreading) << "All" << totalThreads << "threads stopped successfully";
        } else {
            qCDebug(lcThreading) << "Only" << stoppedCount << "out of" << totalThreads << "threads stopped successfully";
        }
    } else {
        // 异步停止：并行发送停止信号
        for ( const QString& name : threadNames ) {
            locker.unlock();
            stopThread(name, false);
            locker.relock();
        }
        qCDebug(lcThreading) << "Stop signals sent to all" << totalThreads << "threads";
    }
}

void ThreadManager::pauseAllThreads() {
    QMutexLocker locker(&m_mutex);

    for ( auto it = m_threads.begin(); it != m_threads.end(); ++it ) {
        const QString& name = it.key();
        locker.unlock();
        pauseThread(name);
        locker.relock();
    }

    qCDebug(lcThreading) << "All threads paused";
}

void ThreadManager::resumeAllThreads() {
    QMutexLocker locker(&m_mutex);

    for ( auto it = m_threads.begin(); it != m_threads.end(); ++it ) {
        const QString& name = it.key();
        locker.unlock();
        resumeThread(name);
        locker.relock();
    }

    qCDebug(lcThreading) << "All threads resumed";
}

void ThreadManager::destroyAllThreads() {
    // 统一走安全的销毁流程：先停止，再逐个destroy，避免删除仍在运行的QThread
    QStringList threadNames;
    {
        QMutexLocker locker(&m_mutex);
        threadNames = m_threads.keys();
    }

    if ( threadNames.isEmpty() ) {
        qCDebug(lcThreading) << "No threads to destroy";
        return;
    }

    qCDebug(lcThreading) << "Destroying" << threadNames.size() << "threads...";

    // 先尝试优雅停止所有线程
    stopAllThreads(true);

    int destroyedCount = 0;
    int abortedCount = 0;
    for ( const QString& name : threadNames ) {
        // 使用已有的安全销毁逻辑，必要时会尝试强制终止，并在未完全停止时中止删除
        if ( destroyThread(name) ) {
            destroyedCount++;
        } else {
            abortedCount++;
            qCWarning(lcThreading) << "DestroyAllThreads: failed to destroy thread (still running or missing):" << name;
        }
    }

    qCDebug(lcThreading) << "DestroyAllThreads summary: destroyed" << destroyedCount << ", aborted" << abortedCount << ", total" << threadNames.size();
}

bool ThreadManager::hasThread(const QString& name) const {
    QMutexLocker locker(&m_mutex);
    return m_threads.contains(name);
}

const ThreadManager::ThreadInfo* ThreadManager::getThreadInfo(const QString& name) const {
    QMutexLocker locker(&m_mutex);
    return findThreadInfo(name);
}

QStringList ThreadManager::getThreadNames() const {
    QMutexLocker locker(&m_mutex);
    return m_threads.keys();
}

ThreadManager::ThreadStats ThreadManager::getThreadStats() const {
    QMutexLocker locker(&m_mutex);

    ThreadStats stats;
    stats.totalThreads = m_threads.size();

    quint64 totalUptime = 0;
    int runningSamples = 0;

    for ( auto it = m_threads.begin(); it != m_threads.end(); ++it ) {
        const ThreadInfo* info = it.value();

        if ( info->thread->isRunning() ) {
            Worker::State state = info->worker->state();
            switch ( state ) {
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
            if ( info->startedTime.isValid() ) {
                quint64 uptime = info->startedTime.msecsTo(QDateTime::currentDateTime());
                totalUptime += uptime;
                runningSamples++;
            }
        } else {
            stats.stoppedThreads++;
        }
    }

    stats.totalUptime = totalUptime;
    if ( runningSamples > 0 ) {
        stats.averageUptime = totalUptime / runningSamples;
    }

    return stats;
}

Worker* ThreadManager::getWorker(const QString& name) const {
    QMutexLocker locker(&m_mutex);

    const ThreadInfo* info = findThreadInfo(name);
    return info ? info->worker : nullptr;
}

void ThreadManager::setMonitoringInterval(int intervalMs) {
    QMutexLocker locker(&m_mutex);

    m_monitoringInterval = intervalMs;

    if ( m_monitoringEnabled && m_monitoringTimer->isActive() ) {
        m_monitoringTimer->stop();
        m_monitoringTimer->start(m_monitoringInterval);
    }
}

int ThreadManager::monitoringInterval() const {
    QMutexLocker locker(&m_mutex);
    return m_monitoringInterval;
}

void ThreadManager::setMonitoringEnabled(bool enabled) {
    QMutexLocker locker(&m_mutex);

    m_monitoringEnabled = enabled;

    if ( enabled && !m_monitoringTimer->isActive() ) {
        m_monitoringTimer->start(m_monitoringInterval);
    } else if ( !enabled && m_monitoringTimer->isActive() ) {
        m_monitoringTimer->stop();
    }
}

bool ThreadManager::isMonitoringEnabled() const {
    QMutexLocker locker(&m_mutex);
    return m_monitoringEnabled;
}

void ThreadManager::onWorkerStarted() {
    Worker* worker = qobject_cast<Worker*>(sender());
    if ( !worker ) return;

    QString name = getThreadNameByWorker(worker);
    if ( !name.isEmpty() ) {
        emit threadStarted(name);
    }
}

void ThreadManager::onWorkerStopped() {
    qCDebug(lcThreading) << "ThreadManager::onWorkerStopped() called";

    QObject* senderObj = sender();
    if ( !senderObj ) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerStopped() - No sender object, signal likely from deleted worker";
        return;
    }

    // 安全地检查sender是否仍然是有效的Worker对象
    Worker* worker = qobject_cast<Worker*>(senderObj);
    if ( !worker ) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerStopped() - Invalid sender, not a Worker object";
        return;
    }

    // 检查Worker对象是否仍然在我们的线程列表中
    QMutexLocker locker(&m_mutex);
    QString threadName;
    bool found = false;
    ThreadInfo* infoPtr = nullptr;

    for ( auto it = m_threads.begin(); it != m_threads.end(); ++it ) {
        if ( it.value()->worker == worker ) {
            threadName = it.key();
            infoPtr = it.value();
            found = true;
            break;
        }
    }

    if ( !found ) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerStopped() - Worker not found in thread list, ignoring signal";
        return;
    }

    // 记录是否应进行自动重启（需在持锁状态下读取，以保证一致性）
    bool shouldAutoRestart = false;
    if ( infoPtr ) {
        shouldAutoRestart = infoPtr->autoRestart && !infoPtr->stopRequested;
    }

    // 解锁后再发射信号与触发重启，避免死锁
    locker.unlock();

    qCDebug(lcThreading) << "ThreadManager::onWorkerStopped() - threadStopped signal emitted for:" << threadName;
    emit threadStopped(threadName);

    // 如果允许自动重启，尝试重启
    if ( shouldAutoRestart ) {
        tryAutoRestart(threadName);
    }
}

void ThreadManager::onWorkerPaused() {
    qCDebug(lcThreading) << "ThreadManager::onWorkerPaused() called";
    Worker* worker = qobject_cast<Worker*>(sender());
    if ( !worker ) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerPaused() - No worker sender";
        return;
    }

    QString name = getThreadNameByWorker(worker);
    qCDebug(lcThreading) << "ThreadManager::onWorkerPaused() - Worker name:" << name;
    if ( !name.isEmpty() ) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerPaused() - Emitting threadPaused signal for:" << name;
        emit threadPaused(name);
        qCDebug(lcThreading) << "ThreadManager::onWorkerPaused() - threadPaused signal emitted";
    }
}

void ThreadManager::onWorkerResumed() {
    qCDebug(lcThreading) << "ThreadManager::onWorkerResumed() called";
    Worker* worker = qobject_cast<Worker*>(sender());
    if ( !worker ) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerResumed() - No worker sender";
        return;
    }

    QString name = getThreadNameByWorker(worker);
    qCDebug(lcThreading) << "ThreadManager::onWorkerResumed() - Worker name:" << name;
    if ( !name.isEmpty() ) {
        qCDebug(lcThreading) << "ThreadManager::onWorkerResumed() - Emitting threadResumed signal for:" << name;
        emit threadResumed(name);
        qCDebug(lcThreading) << "ThreadManager::onWorkerResumed() - threadResumed signal emitted";
    }
}

void ThreadManager::onWorkerError(const QString& error) {
    Worker* worker = qobject_cast<Worker*>(sender());
    if ( !worker ) return;

    QString name = getThreadNameByWorker(worker);
    if ( !name.isEmpty() ) {
        emit threadError(name, error);
    }
}

void ThreadManager::onMonitoringTimer() {
    ThreadStats stats = getThreadStats();
    emit performanceStatsUpdated(stats);
}

ThreadManager::ThreadInfo* ThreadManager::findThreadInfo(const QString& name) {
    auto it = m_threads.find(name);
    return (it != m_threads.end()) ? it.value() : nullptr;
}

const ThreadManager::ThreadInfo* ThreadManager::findThreadInfo(const QString& name) const {
    auto it = m_threads.find(name);
    return (it != m_threads.end()) ? it.value() : nullptr;
}

QString ThreadManager::getThreadNameByWorker(Worker* worker) const {
    QMutexLocker locker(&m_mutex);

    for ( auto it = m_threads.begin(); it != m_threads.end(); ++it ) {
        if ( it.value()->worker == worker ) {
            return it.key();
        }
    }

    return QString();
}

void ThreadManager::connectWorkerSignals(Worker* worker) {
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

    if ( !result2 ) {
        qCDebug(lcThreading) << "ThreadManager::connectWorkerSignals() - FAILED to connect stopped signal for worker:" << worker->name();
    }

    // 验证连接是否真的建立
    // 注意：避免捕获裸指针，防止对象已删除时的悬空访问。使用QPointer进行弱引用保护。
    QPointer<Worker> workerGuard(worker);
    QMetaObject::Connection conn = connect(worker, &Worker::stopped, this, [workerGuard]() {
        const QString n = workerGuard ? workerGuard->name() : QStringLiteral("<deleted>");
        qCDebug(lcThreading) << "Lambda slot received stopped signal from:" << n;
    }, Qt::AutoConnection);
    qCDebug(lcThreading) << "ThreadManager::connectWorkerSignals() - Lambda connection valid:" << (bool)conn;
}

void ThreadManager::disconnectWorkerSignals(Worker* worker) {
    if ( !worker ) return;
    worker->disconnect(this);
}

void ThreadManager::tryAutoRestart(const QString& name) {
    QMutexLocker locker(&m_mutex);

    ThreadInfo* info = findThreadInfo(name);
    if ( !info || !info->autoRestart ) {
        return;
    }

    // 检查重启次数限制
    if ( info->maxRestarts >= 0 && info->restartCount >= info->maxRestarts ) {
        qCDebug(lcThreading) << "Thread" << name << "reached maximum restart limit:" << info->maxRestarts; // 统一为debug
        return;
    }

    info->restartCount++;

    qCDebug(lcThreading) << "Auto-restarting thread" << name << "(attempt" << info->restartCount << ")";

    // 延迟重启以避免快速循环
    QTimer::singleShot(1000, [this, name]() {
        // 注意：不要在lambda中持锁过长时间，先判断线程存在，再根据状态选择重启方式
        if ( !hasThread(name) ) {
            return;
        }

        // 如果底层QThread仍在运行，则直接在该线程中重新启动Worker
        const ThreadInfo* cinfo = findThreadInfo(name);
        if ( cinfo && cinfo->thread && cinfo->thread->isRunning() ) {
            QMetaObject::invokeMethod(cinfo->worker, "start", Qt::QueuedConnection);
        } else {
            // 如果线程已停止，则按原有流程重新启动线程
            startThread(name);
        }

        const ThreadInfo* finfo = findThreadInfo(name);
        int count = finfo ? finfo->restartCount : 0;
        emit threadRestarted(name, count);
    });
}