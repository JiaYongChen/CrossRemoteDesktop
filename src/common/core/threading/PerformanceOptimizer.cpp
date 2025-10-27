#include "PerformanceOptimizer.h"
#include "ThreadManager.h"
#include <QtCore/QThread>
#include <QtCore/QMutexLocker>
#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#endif

Q_LOGGING_CATEGORY(performanceOptimizer, "performance.optimizer")

// 静态成员初始化
PerformanceOptimizer* PerformanceOptimizer::s_instance = nullptr;

PerformanceOptimizer* PerformanceOptimizer::instance()
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    
    if (!s_instance) {
        s_instance = new PerformanceOptimizer();
    }
    
    return s_instance;
}

PerformanceOptimizer::PerformanceOptimizer(QObject *parent)
    : QObject(parent)
    , m_monitoringTimer(new QTimer(this))
    , m_autoOptimizationTimer(new QTimer(this))
    , m_threadManager(nullptr)
    , m_isMonitoring(false)
{
    qCDebug(performanceOptimizer, "PerformanceOptimizer 初始化");
    
    // 设置默认配置
    m_config = PerformanceConfig();
    
    // 连接定时器信号
    connect(m_monitoringTimer, &QTimer::timeout, this, &PerformanceOptimizer::onMonitoringTimer);
    connect(m_autoOptimizationTimer, &QTimer::timeout, this, &PerformanceOptimizer::onAutoOptimizationTimer);
    
    // 获取ThreadManager引用
    m_threadManager = ThreadManager::instance();
    
    // 初始化统计信息
    m_stats.lastUpdated = QDateTime::currentDateTime();
    m_startTime = QDateTime::currentDateTime();
}

PerformanceOptimizer::~PerformanceOptimizer()
{
    qCDebug(performanceOptimizer, "PerformanceOptimizer 析构");
    stopMonitoring();
}

void PerformanceOptimizer::setConfig(const PerformanceConfig& config)
{
    QMutexLocker locker(&m_mutex);
    
    qCDebug(performanceOptimizer, "设置性能配置");
    m_config = config;
    
    // 更新定时器间隔
    if (m_isMonitoring) {
        m_monitoringTimer->setInterval(m_config.monitoringInterval);
    }
    
    // 如果启用自动优化，启动自动优化定时器
    if (m_config.enableAutoOptimization && !m_autoOptimizationTimer->isActive()) {
        m_autoOptimizationTimer->start(m_config.monitoringInterval * 5); // 每5个监控周期执行一次自动优化
    } else if (!m_config.enableAutoOptimization && m_autoOptimizationTimer->isActive()) {
        m_autoOptimizationTimer->stop();
    }
}

PerformanceOptimizer::PerformanceConfig PerformanceOptimizer::getConfig() const
{
    QMutexLocker locker(&m_mutex);
    return m_config;
}

void PerformanceOptimizer::startMonitoring()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isMonitoring) {
        qCDebug(performanceOptimizer, "性能监控已在运行");
        return;
    }
    
    qCDebug(performanceOptimizer, "启动性能监控");
    m_isMonitoring = true;
    m_startTime = QDateTime::currentDateTime();
    
    // 启动监控定时器
    m_monitoringTimer->start(m_config.monitoringInterval);
    
    // 如果启用自动优化，启动自动优化定时器
    if (m_config.enableAutoOptimization) {
        m_autoOptimizationTimer->start(m_config.monitoringInterval * 5);
    }
    
    // 立即执行一次监控
    onMonitoringTimer();
}

void PerformanceOptimizer::stopMonitoring()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_isMonitoring) {
        return;
    }
    
    qCDebug(performanceOptimizer, "停止性能监控");
    m_isMonitoring = false;
    
    // 停止定时器
    m_monitoringTimer->stop();
    m_autoOptimizationTimer->stop();
}

PerformanceOptimizer::PerformanceStats PerformanceOptimizer::getStats() const
{
    QMutexLocker locker(&m_mutex);
    return m_stats;
}

void PerformanceOptimizer::resetStats()
{
    QMutexLocker locker(&m_mutex);
    
    qCDebug(performanceOptimizer, "重置性能统计");
    
    // 重置统计数据
    m_stats = PerformanceStats();
    m_stats.lastUpdated = QDateTime::currentDateTime();
    
    // 清空历史数据
    m_cpuHistory.clear();
    m_memoryHistory.clear();
    m_queueSizeHistory.clear();
    
    m_startTime = QDateTime::currentDateTime();
}

void PerformanceOptimizer::optimizeThreadPriority(const QString& threadName, ThreadPriority priority)
{
    qCDebug(performanceOptimizer) << "优化线程优先级:" << threadName << "优先级:" << static_cast<int>(priority);
    
    if (!m_threadManager) {
        qCDebug(performanceOptimizer, "ThreadManager 未初始化");
        return;
    }
    
    // 将优先级映射到Qt线程优先级
    QThread::Priority qtPriority = QThread::NormalPriority;
    switch (priority) {
    case ThreadPriority::Idle:
        qtPriority = QThread::IdlePriority;
        break;
    case ThreadPriority::Low:
        qtPriority = QThread::LowPriority;
        break;
    case ThreadPriority::Normal:
        qtPriority = QThread::NormalPriority;
        break;
    case ThreadPriority::High:
        qtPriority = QThread::HighPriority;
        break;
    case ThreadPriority::Critical:
        qtPriority = QThread::HighestPriority;
        break;
    }
    
    // 通过ThreadManager设置线程优先级
    // 注意：这里需要ThreadManager提供设置优先级的接口
    // m_threadManager->setThreadPriority(threadName, qtPriority);
    Q_UNUSED(qtPriority)  // 暂时未使用，避免编译警告
}

void PerformanceOptimizer::optimizeQueueSize(const QString& queueName, int optimalSize)
{
    qCDebug(performanceOptimizer) << "优化队列大小:" << queueName << "最优大小:" << optimalSize;
    
    // 确保队列大小在合理范围内
    int adjustedSize = qBound(10, optimalSize, m_config.maxQueueSize);
    
    if (adjustedSize != optimalSize) {
        qCDebug(performanceOptimizer) << "队列大小已调整为:" << adjustedSize;
    }
    
    // 这里可以通过信号通知相关组件调整队列大小
    emit optimizationSuggestion(QString("建议调整队列 %1 大小为 %2").arg(queueName).arg(adjustedSize));
}

void PerformanceOptimizer::triggerMemoryCleanup()
{
    qCDebug(performanceOptimizer, "触发内存清理");
    
    // 强制垃圾回收（如果使用Qt的垃圾回收机制）
    QCoreApplication::processEvents();
    
    // 通知所有组件进行内存清理
    emit optimizationSuggestion("建议执行内存清理操作");
}

void PerformanceOptimizer::autoOptimize()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_config.enableAutoOptimization) {
        return;
    }
    
    qCDebug(performanceOptimizer, "执行自动优化");
    
    // 检查性能阈值
    checkPerformanceThresholds();
    
    // 生成优化建议
    generateOptimizationSuggestions();
    
    // 应用自动优化
    applyAutoOptimizations();
}

void PerformanceOptimizer::setThreadAffinity(const QString& threadName, int cpuCore)
{
    qCDebug(performanceOptimizer) << "设置线程亲和性:" << threadName << "CPU核心:" << cpuCore;
    
    if (!m_config.enableThreadAffinity) {
        qCDebug(performanceOptimizer, "线程亲和性未启用");
        emit optimizationSuggestion(QString("线程亲和性功能已禁用，无法设置线程 %1 的CPU亲和性").arg(threadName));
        return;
    }
    
    // 在macOS上设置线程亲和性比较复杂，这里提供基本框架
    // 实际实现需要使用系统特定的API
    emit optimizationSuggestion(QString("建议将线程 %1 绑定到CPU核心 %2").arg(threadName).arg(cpuCore));
}

void PerformanceOptimizer::onMonitoringTimer()
{
    if (!m_isMonitoring) {
        return;
    }
    
    // 更新各项性能指标
    updateCpuUsage();
    updateMemoryUsage();
    updateThreadStats();
    updateQueueStats();
    
    // 更新时间戳
    m_stats.lastUpdated = QDateTime::currentDateTime();
    
    // 发送性能统计更新信号
    emit performanceStatsUpdated(m_stats);
    
    // 记录性能日志
    if (m_config.enablePerformanceLogging) {
        qCDebug(performanceOptimizer) << "性能统计 - CPU:" << m_stats.cpuUsage << "% 内存:" 
                                      << (m_stats.memoryUsage / (1024 * 1024)) << "MB 活跃线程:" << m_stats.activeThreads;
    }
}

void PerformanceOptimizer::onAutoOptimizationTimer()
{
    autoOptimize();
}

void PerformanceOptimizer::updateCpuUsage()
{
#ifdef Q_OS_MACOS
    // macOS特定的CPU使用率获取
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, 
                       (host_info_t)&cpuinfo, &count) == KERN_SUCCESS) {
        
        static unsigned int lastUser = 0, lastSystem = 0, lastIdle = 0;
        
        unsigned int user = cpuinfo.cpu_ticks[CPU_STATE_USER];
        unsigned int system = cpuinfo.cpu_ticks[CPU_STATE_SYSTEM];
        unsigned int idle = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
        
        unsigned int totalTicks = (user - lastUser) + (system - lastSystem) + (idle - lastIdle);
        
        if (totalTicks > 0) {
            double usage = ((double)((user - lastUser) + (system - lastSystem)) / totalTicks) * 100.0;
            m_stats.cpuUsage = qBound(0.0, usage, 100.0);
        }
        
        lastUser = user;
        lastSystem = system;
        lastIdle = idle;
    }
#else
    // 其他平台的CPU使用率获取（简化实现）
    m_stats.cpuUsage = 0.0;
#endif
    
    // 更新平均CPU使用率
    QList<double>& cpuHistory = m_cpuHistory["system"];
    cpuHistory.append(m_stats.cpuUsage);
    
    // 保持历史数据在合理范围内
    if (cpuHistory.size() > 60) { // 保留最近60个数据点
        cpuHistory.removeFirst();
    }
    
    // 计算平均值
    if (!cpuHistory.isEmpty()) {
        double sum = 0.0;
        for (double usage : cpuHistory) {
            sum += usage;
        }
        m_stats.averageCpuUsage = sum / cpuHistory.size();
    }
}

void PerformanceOptimizer::updateMemoryUsage()
{
#ifdef Q_OS_MACOS
    // macOS特定的内存使用获取
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, 
                  (task_info_t)&info, &count) == KERN_SUCCESS) {
        m_stats.memoryUsage = info.resident_size;
        
        // 更新峰值内存使用
        if (m_stats.memoryUsage > m_stats.peakMemoryUsage) {
            m_stats.peakMemoryUsage = m_stats.memoryUsage;
        }
        
        // 计算内存使用百分比
        if (m_config.maxMemoryUsage > 0) {
            m_stats.memoryUsagePercent = (double)m_stats.memoryUsage / m_config.maxMemoryUsage * 100.0;
        }
    }
#else
    // 其他平台的内存使用获取（简化实现）
    m_stats.memoryUsage = 0;
#endif
    
    // 更新内存使用历史
    QList<size_t>& memoryHistory = m_memoryHistory["system"];
    memoryHistory.append(m_stats.memoryUsage);
    
    if (memoryHistory.size() > 60) {
        memoryHistory.removeFirst();
    }
}

void PerformanceOptimizer::updateThreadStats()
{
    if (!m_threadManager) {
        return;
    }
    
    // 获取线程统计信息
    // 注意：这里需要ThreadManager提供获取线程统计的接口
    // auto threadStats = m_threadManager->getThreadStats();
    // m_stats.activeThreads = threadStats.activeCount;
    // m_stats.totalThreads = threadStats.totalCount;
    
    // 临时实现：使用QThread的活跃线程数
    m_stats.activeThreads = QThread::idealThreadCount();
    m_stats.totalThreads = m_stats.activeThreads;
    
    // 计算线程效率
    if (m_stats.totalThreads > 0) {
        m_stats.threadEfficiency = (double)m_stats.activeThreads / m_stats.totalThreads * 100.0;
    }
}

void PerformanceOptimizer::updateQueueStats()
{
    // 这里需要从各个队列收集统计信息
    // 由于队列分布在不同的组件中，可能需要通过信号槽机制收集
    
    // 临时实现：模拟队列统计
    m_stats.totalQueueSize = 0;
    m_stats.averageQueueSize = 0;
    m_stats.queueThroughput = 0.0;
    
    // 更新队列大小历史
    QList<int>& queueHistory = m_queueSizeHistory["total"];
    queueHistory.append(m_stats.totalQueueSize);
    
    if (queueHistory.size() > 60) {
        queueHistory.removeFirst();
    }
    
    // 计算平均队列大小
    if (!queueHistory.isEmpty()) {
        int sum = 0;
        for (int size : queueHistory) {
            sum += size;
        }
        m_stats.averageQueueSize = sum / queueHistory.size();
    }
}

void PerformanceOptimizer::checkPerformanceThresholds()
{
    // 检查CPU使用率
    if (m_stats.cpuUsage > 80.0) {
        emit performanceWarning(QString("CPU使用率过高: %1%").arg(m_stats.cpuUsage, 0, 'f', 1));
    }
    
    // 检查内存使用
    if (m_stats.memoryUsage > m_config.memoryWarningThreshold) {
        emit memoryWarning(m_stats.memoryUsage, m_config.memoryWarningThreshold);
    }
    
    // 检查线程效率
    if (m_stats.threadEfficiency < 50.0) {
        emit performanceWarning(QString("线程效率较低: %1%").arg(m_stats.threadEfficiency, 0, 'f', 1));
    }
}

void PerformanceOptimizer::generateOptimizationSuggestions()
{
    QStringList suggestions;
    
    // 基于CPU使用率的建议
    if (m_stats.cpuUsage > 70.0) {
        suggestions << "建议降低线程优先级或减少并发任务";
    } else if (m_stats.cpuUsage < 30.0) {
        suggestions << "CPU使用率较低，可以增加并发任务";
    }
    
    // 基于内存使用的建议
    if (m_stats.memoryUsagePercent > 80.0) {
        suggestions << "建议执行内存清理或增加内存限制";
    }
    
    // 基于线程效率的建议
    if (m_stats.threadEfficiency < 60.0) {
        suggestions << "建议优化线程调度策略";
    }
    
    // 发送建议
    for (const QString& suggestion : suggestions) {
        emit optimizationSuggestion(suggestion);
    }
}

void PerformanceOptimizer::applyAutoOptimizations()
{
    // 自动调整线程优先级
    if (m_stats.cpuUsage > 80.0) {
        // 降低非关键线程的优先级
        optimizeThreadPriority("ScreenCaptureWorker", ThreadPriority::Low);
    }
    
    // 自动触发内存清理
    if (m_stats.memoryUsagePercent > 85.0) {
        triggerMemoryCleanup();
    }
    
    // 自动调整队列大小
    if (m_stats.averageQueueSize > m_config.defaultQueueSize * 2) {
        optimizeQueueSize("ScreenCaptureQueue", m_config.defaultQueueSize);
    }
}