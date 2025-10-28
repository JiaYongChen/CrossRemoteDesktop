#include "DataProcessingWorker.h"
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>
#include <QtCore/QIODevice>
#include <QtCore/QBuffer>
#include <QtConcurrent/QtConcurrent>
#include <cstring>
#include <algorithm>

Q_LOGGING_CATEGORY(lcDataProcessingWorker, "dataprocessingworker", QtDebugMsg)

DataProcessingWorker::DataProcessingWorker(QObject* parent)
    : Worker(parent)
    , m_queueManager(nullptr)
    , m_captureQueue(nullptr)
    , m_processedQueue(nullptr)
    , m_config(nullptr)
    , m_dataProcessor(nullptr)
    , m_statsTimer(nullptr)
    , m_processedFrames(0)
    , m_droppedFrames(0)
    , m_totalProcessingTime(0)
    , m_averageLatency(0.0)
    , m_processingRate(0.0)
    , m_lastStatsUpdate(0)
    , m_processingTimeout(DEFAULT_PROCESSING_TIMEOUT)
    , m_maxQueueSize(100)
    , m_statsUpdateInterval(DEFAULT_STATS_INTERVAL)
    , m_maxRetries(3)
    , m_retryDelayMs(100)
    , m_retryCount(0)
    , m_maxLatencyThreshold(MAX_PROCESSING_LATENCY)
    , m_minRateThreshold(MIN_PROCESSING_RATE)
    , m_cpuUsage(0.0)
    , m_memoryUsage(0.0)
    , m_resourceMonitorTimer(nullptr)
    , m_adaptiveMode(true)
    , m_adaptiveTimer(nullptr)
    , m_maxParallelTasks(QThread::idealThreadCount())
    , m_activeParallelTasks(0) {
    qCDebug(lcDataProcessingWorker) << "DataProcessingWorker 构造函数";
    qCInfo(lcDataProcessingWorker) << "并行处理线程数:" << m_maxParallelTasks;
}

DataProcessingWorker::~DataProcessingWorker() {
    qCDebug(lcDataProcessingWorker) << "DataProcessingWorker析构函数";
    // 确保在正确的线程中停止定时器与清理，避免跨线程 killTimer 警告
    QThread* workerThread = this->thread();
    QThread* current = QThread::currentThread();
    if ( workerThread && workerThread->isRunning() && current != workerThread ) {
        // 所属线程仍在运行，阻塞式投递到所属线程
        QMetaObject::invokeMethod(this, [this]() { cleanup(); }, Qt::BlockingQueuedConnection);
    } else if ( workerThread && current != workerThread ) {
        // 所属线程已停止或不一致，先迁移到当前线程再清理
        this->moveToThread(current);
        cleanup();
    } else {
        // 已在所属线程或无效线程，直接清理
        cleanup();
    }
}

void DataProcessingWorker::setProcessingConfig(std::shared_ptr<DataProcessingConfig> config) {
    qCDebug(lcDataProcessingWorker) << "设置处理配置";
    m_config = config;
}

std::shared_ptr<DataProcessingConfig> DataProcessingWorker::getProcessingConfig() const {
    return m_config;
}

QString DataProcessingWorker::getProcessingStats() const {
    QMutexLocker locker(&m_statsMutex);

    return QString("已处理帧数: %1, 丢弃帧数: %2, 平均延迟: %3ms, 处理速率: %4fps")
        .arg(m_processedFrames.load())
        .arg(m_droppedFrames.load())
        .arg(m_averageLatency.load(), 0, 'f', 2)
        .arg(m_processingRate.load(), 0, 'f', 2);
}

double DataProcessingWorker::getProcessingRate() const {
    return m_processingRate.load();
}

double DataProcessingWorker::getAverageProcessingLatency() const {
    return m_averageLatency.load();
}

void DataProcessingWorker::setProcessingTimeout(int timeoutMs) {
    qCDebug(lcDataProcessingWorker) << "设置处理超时时间:" << timeoutMs << "毫秒";
    m_processingTimeout = timeoutMs;
}

void DataProcessingWorker::setMaxQueueSize(int maxSize) {
    qCDebug(lcDataProcessingWorker) << "设置最大队列大小:" << maxSize;
    m_maxQueueSize = maxSize;

    if ( m_queueManager ) {
        m_queueManager->setQueueMaxSize(QueueManager::CaptureQueue, maxSize);
    }
}

bool DataProcessingWorker::initialize() {
    qCDebug(lcDataProcessingWorker) << "初始化 DataProcessingWorker";

    try {
        // 获取队列管理器实例
        QueueManager* queueManager = QueueManager::instance();
        if ( !queueManager ) {
            qCCritical(lcDataProcessingWorker) << "无法获取队列管理器实例";
            return false;
        }
        // 保存队列管理器指针，便于后续断开信号连接
        m_queueManager = queueManager;

        // 获取捕获队列和处理队列
        m_captureQueue = queueManager->getCaptureQueue();
        m_processedQueue = queueManager->getProcessedQueue();

        if ( !m_captureQueue || !m_processedQueue ) {
            qCCritical(lcDataProcessingWorker) << "无法获取队列实例";
            return false;
        }

        // 连接队列信号
        connect(queueManager, &QueueManager::queueWarning,
            this, &DataProcessingWorker::onQueueWarning);
        connect(queueManager, &QueueManager::queueError,
            this, &DataProcessingWorker::onQueueError);

        // 创建数据处理器
        m_dataProcessor = std::make_unique<DataProcessor>(this);
        if ( !m_dataProcessor ) {
            qCCritical(lcDataProcessingWorker) << "无法创建数据处理器";
            return false;
        }

        // 创建统计更新定时器
        m_statsTimer = new QTimer(this);
        m_statsTimer->setInterval(m_statsUpdateInterval);
        connect(m_statsTimer, &QTimer::timeout, this, &DataProcessingWorker::updateStats);
        m_statsTimer->start();

        // 创建资源监控定时器
        m_resourceMonitorTimer = new QTimer(this);
        m_resourceMonitorTimer->setInterval(5000); // 每5秒检查一次
        connect(m_resourceMonitorTimer, &QTimer::timeout, this, &DataProcessingWorker::checkSystemResources);
        m_resourceMonitorTimer->start();

        // 创建自适应调整定时器
        if ( m_adaptiveMode ) {
            m_adaptiveTimer = new QTimer(this);
            m_adaptiveTimer->setInterval(10000); // 每10秒调整一次
            connect(m_adaptiveTimer, &QTimer::timeout, this, &DataProcessingWorker::adaptProcessingParameters);
            m_adaptiveTimer->start();
        }

        // 初始化性能计时器
        m_performanceTimer.start();
        m_lastStatsUpdate = m_performanceTimer.elapsed();

        qCInfo(lcDataProcessingWorker) << "DataProcessingWorker 初始化成功";
        return true;

    } catch ( const std::exception& e ) {
        qCCritical(lcDataProcessingWorker) << "初始化异常:" << e.what();
        return false;
    } catch ( ... ) {
        qCCritical(lcDataProcessingWorker) << "初始化未知异常";
        return false;
    }
}

void DataProcessingWorker::stop(bool waitForFinish) {
    qCDebug(lcDataProcessingWorker) << "停止DataProcessingWorker，立即禁用自适应模式";

    // 立即禁用自适应模式，防止在停止过程中继续调整参数
    m_adaptiveMode = false;
    qCDebug(lcDataProcessingWorker) << "自适应模式已立即禁用";

    // 调用父类的stop方法
    Worker::stop(waitForFinish);
}

void DataProcessingWorker::cleanup() {
    qCDebug(lcDataProcessingWorker) << "清理DataProcessingWorker";

    // 确保自适应模式已禁用（在stop方法中已经设置，这里是双重保险）
    m_adaptiveMode = false;
    qCDebug(lcDataProcessingWorker) << "自适应模式已禁用";

    // 停止处理并清空队列，确保processTask能快速退出
    stopProcessingAndClearQueues();
    qCDebug(lcDataProcessingWorker) << "已停止处理并清空队列";

    // 停止所有定时器
    if ( m_statsTimer && m_statsTimer->isActive() ) {
        m_statsTimer->stop();
        qCDebug(lcDataProcessingWorker) << "统计定时器已停止";
    }

    if ( m_resourceMonitorTimer && m_resourceMonitorTimer->isActive() ) {
        m_resourceMonitorTimer->stop();
        qCDebug(lcDataProcessingWorker) << "资源监控定时器已停止";
    }

    if ( m_adaptiveTimer && m_adaptiveTimer->isActive() ) {
        m_adaptiveTimer->stop();
        qCDebug(lcDataProcessingWorker) << "自适应调整定时器已停止";
    }

    // 断开队列管理器信号连接
    if ( m_queueManager ) {
        disconnect(m_queueManager, nullptr, this, nullptr);
    }

    // 清理数据处理器
    m_dataProcessor.reset();

    // 重置队列引用
    m_captureQueue = nullptr;
    m_processedQueue = nullptr;
    m_queueManager = nullptr;

    Worker::cleanup();
    qCDebug(lcDataProcessingWorker) << "DataProcessingWorker清理完成";
}

void DataProcessingWorker::processTask() {
    // 首先检查是否应该停止
    if ( shouldStop() ) {
        qCDebug(lcDataProcessingWorker) << "检测到停止信号，退出processTask";
        return;
    }

    if ( !m_captureQueue || !m_processedQueue ) {
        return;
    }

    try {
        // 批量处理帧数据以提高效率 - 动态调整批大小
        const int maxBatchSize = std::min(m_maxParallelTasks * 2, 10); // 最多10帧
        // qCDebug(lcDataProcessingWorker) << "当前批量处理最大帧数:" << maxBatchSize;
        std::vector<CapturedFrame> frameBatch;
        frameBatch.reserve(maxBatchSize);

        QElapsedTimer batchTimer;
        batchTimer.start();

        // 自动处理机制：使用带超时的阻塞式获取第一帧数据
        CapturedFrame firstFrame;
        bool hasFirstFrame = false;

        // 第一次获取使用带超时的阻塞方式，实现自动处理
        if ( m_captureQueue->dequeue(firstFrame, 100) ) { // 100ms超时
            // 获取到数据后再次检查停止状态
            if ( shouldStop() ) {
                qCDebug(lcDataProcessingWorker) << "获取帧数据后检测到停止信号，退出处理";
                return;
            }

            hasFirstFrame = true;
            frameBatch.push_back(std::move(firstFrame));
        }

        // 如果获取到第一帧，继续收集更多帧进行批量处理
        if ( hasFirstFrame ) {
            while ( frameBatch.size() < static_cast<size_t>(maxBatchSize) ) {
                CapturedFrame additionalFrame;
                if ( !m_captureQueue->tryDequeue(additionalFrame) ) {
                    // 队列为空，退出收集
                    // qCDebug(lcDataProcessingWorker) << "捕获队列为空，退出批量收集";
                    break;
                }
                frameBatch.push_back(std::move(additionalFrame));

                // 检查是否需要停止
                if ( shouldStop() ) {
                    qCDebug(lcDataProcessingWorker) << "检测到停止信号，退出批量收集";
                    break;
                }
            }

            // 使用并行处理批量帧
            if ( !frameBatch.empty() ) {
                int processedCount = processBatchParallel(frameBatch);

                // qint64 batchTime = batchTimer.elapsed();
                if ( processedCount > 0 ) {
                    // qCDebug(lcDataProcessingWorker) << "并行批处理完成，处理帧数:" << processedCount
                    //     << "/" << frameBatch.size()
                    //     << "总时间:" << batchTime << "ms"
                    //     << "平均每帧:" << (batchTime / processedCount) << "ms"
                    //     << "活跃线程:" << m_activeParallelTasks.load();
                }
            }
        }

        // 定期检查系统资源和性能
        static int taskCount = 0;
        if ( ++taskCount % 50 == 0 ) { // 每50次任务检查一次（减少频率因为批处理更高效）
            // 在检查系统资源前也要确认没有停止信号
            if ( shouldStop() ) {
                qCDebug(lcDataProcessingWorker) << "检测到停止信号，跳过系统资源检查";
                return;
            }

            checkSystemResources();
            checkPerformance();

            // 如果启用自适应模式且没有停止信号，调整处理参数
            if ( m_adaptiveMode && !shouldStop() ) {
                adaptProcessingParameters();
            }
        }

    } catch ( const std::exception& e ) {
        qCCritical(lcDataProcessingWorker) << "processTask异常:" << e.what();
        emit processingError(QString("数据处理任务异常: %1").arg(e.what()));

        // 异常后短暂休眠，避免连续异常导致CPU占用过高
        QThread::msleep(10);
    } catch ( ... ) {
        qCCritical(lcDataProcessingWorker) << "processTask未知异常";
        emit processingError("数据处理任务发生未知异常");

        // 异常后短暂休眠，避免连续异常导致CPU占用过高
        QThread::msleep(10);
    }
}


int DataProcessingWorker::processBatchParallel(const std::vector<CapturedFrame>& frames) {
    if ( frames.empty() ) {
        return 0;
    }

    QElapsedTimer timer;
    timer.start();

    int successCount = 0;
    int droppedCount = 0;

    // 使用 QtConcurrent 并行处理所有帧
    QList<CapturedFrame> frameList;
    for ( const auto& frame : frames ) {
        frameList.append(frame);
    }

    // 并行编码所有图像
    QFuture<ProcessedData> future = QtConcurrent::mapped(frameList,
        [](const CapturedFrame& frame) -> ProcessedData {
        // 验证帧数据
        if ( !frame.isValid() ) {
            qCWarning(lcDataProcessingWorker) << "帧数据无效，ID:" << frame.frameId;
            return ProcessedData(); // 返回无效数据
        }

        // 检查帧延迟
        qint64 latency = frame.getLatency();
        if ( latency > 5000 ) { // 5秒超时
            qCWarning(lcDataProcessingWorker) << "帧延迟过高:" << latency << "ms，ID:" << frame.frameId;
            return ProcessedData();
        }

        // 并行编码图像
        return DataProcessingWorker::encodeImageParallel(frame.image, frame.frameId);
    });

    // 等待所有编码完成
    future.waitForFinished();

    // 收集结果并入队
    QList<ProcessedData> results = future.results();
    for ( const auto& processedData : results ) {
        if ( processedData.isValid() ) {
            // 使用丢弃旧帧策略将结果放入队列
            // 当队列满时，自动丢弃最旧的帧，确保最新数据能够入队
            if ( m_processedQueue->enqueueDropOldest(processedData) ) {
                successCount++;
                m_processedFrames++;
            } else {
                // 队列已停止
                droppedCount++;
                m_droppedFrames++;
                qCWarning(lcDataProcessingWorker) << "处理队列已停止，无法入队，帧ID:" << processedData.originalFrameId;
            }
        } else {
            droppedCount++;
            m_droppedFrames++;
        }
    }

    qint64 elapsed = timer.elapsed();
    if ( successCount > 0 ) {
        m_totalProcessingTime += elapsed;
    }

    // 记录批处理统计信息
    if ( droppedCount > 0 ) {
        // qCWarning(lcDataProcessingWorker) << "批处理完成: 成功" << successCount
        //     << "丢弃" << droppedCount << "耗时" << elapsed << "ms";
    } else if ( successCount > 0 ) {
        // qCDebug(lcDataProcessingWorker) << "批处理完成: 成功" << successCount << "耗时" << elapsed << "ms";
    }

    return successCount;
}

ProcessedData DataProcessingWorker::encodeImageParallel(const QImage& image, quint64 frameId) {
    ProcessedData result;

    try {
        // 使用原始像素数据，避免编码开销
        // 确保图像格式为 RGB32 或 ARGB32，便于客户端解析
        QImage convertedImage = image;
        if ( image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32 ) {
            convertedImage = image.convertToFormat(QImage::Format_RGB32);
        }

        // 直接获取原始像素数据
        const uchar* bits = convertedImage.constBits();
        int dataSize = convertedImage.sizeInBytes();
        
        if ( !bits || dataSize <= 0 ) {
            qCWarning(lcDataProcessingWorker) << "无法获取图像原始数据，帧ID:" << frameId;
            return result;
        }

        // 将原始像素数据复制到 QByteArray
        QByteArray imageData(reinterpret_cast<const char*>(bits), dataSize);

        // 构造ProcessedData
        result.originalFrameId = frameId;
        result.compressedData = imageData;
        result.imageSize = convertedImage.size();
        result.processedTime = QDateTime::currentDateTime();
        result.originalDataSize = convertedImage.sizeInBytes();
        result.compressedDataSize = imageData.size();

    } catch ( const std::exception& e ) {
        qCCritical(lcDataProcessingWorker) << "图像处理异常:" << e.what() << "帧ID:" << frameId;
    } catch ( ... ) {
        qCCritical(lcDataProcessingWorker) << "图像处理未知异常，帧ID:" << frameId;
    }

    return result;
}

void DataProcessingWorker::checkSystemResources() {
    try {
        // 简化的资源监控实现
        // 在实际应用中，可以使用系统API获取真实的CPU和内存使用率

        // 基于处理队列状态估算CPU使用率
        double queueUtilization = 0.0;
        if ( m_captureQueue && m_processedQueue ) {
            int captureSize = m_captureQueue->size();
            int processedSize = m_processedQueue->size();
            queueUtilization = static_cast<double>(captureSize + processedSize) / (m_maxQueueSize * 2) * 100.0;
        }

        // 基于处理速率估算CPU使用率
        double processingLoad = (m_processingRate.load() / 60.0) * 100.0; // 假设60fps为满负载
        m_cpuUsage = std::min(std::max(queueUtilization, processingLoad), 100.0);

        // 基于处理延迟估算内存压力
        double latencyFactor = (m_averageLatency.load() / m_maxLatencyThreshold) * 100.0;
        m_memoryUsage = std::min(latencyFactor, 100.0);

        // 检查资源使用阈值
        if ( m_cpuUsage.load() > MAX_CPU_USAGE ) {
            emit processingWarning(QString("CPU使用率过高: %1%").arg(m_cpuUsage.load(), 0, 'f', 1));
        }

        if ( m_memoryUsage.load() > MAX_MEMORY_USAGE ) {
            emit processingWarning(QString("内存使用率过高: %1%").arg(m_memoryUsage.load(), 0, 'f', 1));
        }

    } catch ( const std::exception& e ) {
        qCWarning(lcDataProcessingWorker) << "资源监控异常:" << e.what();
    } catch ( ... ) {
        qCWarning(lcDataProcessingWorker) << "资源监控未知异常";
    }
}

void DataProcessingWorker::adaptProcessingParameters() {
    // 首先检查是否应该停止
    if ( shouldStop() ) {
        qCDebug(lcDataProcessingWorker) << "检测到停止信号，跳过自适应参数调整";
        return;
    }

    if ( !m_adaptiveMode ) {
        return;
    }

    try {
        double currentLatency = m_averageLatency.load();
        double currentRate = m_processingRate.load();
        double currentCpuUsage = m_cpuUsage.load();

        // 根据性能指标自适应调整参数
        if ( currentLatency > m_maxLatencyThreshold || currentCpuUsage > MAX_CPU_USAGE ) {
            // 性能压力较大，调整处理参数
            qCDebug(lcDataProcessingWorker) << "检测到性能压力，调整处理参数";

            // 增加重试延迟
            m_retryDelayMs = std::min(m_retryDelayMs + 10, 500);

        } else if ( currentLatency < m_maxLatencyThreshold * 0.5 && currentCpuUsage < MAX_CPU_USAGE * 0.5 ) {
            // 性能充足，可以提高处理质量
            qCDebug(lcDataProcessingWorker) << "检测到性能充足，调整处理参数";

            // 减少重试延迟
            m_retryDelayMs = std::max(m_retryDelayMs - 10, 50);
        }

        qCDebug(lcDataProcessingWorker) << "自适应调整完成，延迟:" << currentLatency
            << "ms，速率:" << currentRate
            << "fps，CPU:" << currentCpuUsage << "%";

    } catch ( const std::exception& e ) {
        qCWarning(lcDataProcessingWorker) << "自适应调整异常:" << e.what();
    } catch ( ... ) {
        qCWarning(lcDataProcessingWorker) << "自适应调整未知异常";
    }
}

// 新增的公共方法实现
void DataProcessingWorker::setRetryConfig(int maxRetries, int retryDelayMs) {
    m_maxRetries = std::max(0, maxRetries);
    m_retryDelayMs = std::max(10, retryDelayMs);
    qCDebug(lcDataProcessingWorker) << "重试配置更新，最大重试次数:" << m_maxRetries
        << "重试延迟:" << m_retryDelayMs << "ms";
}

DataProcessingWorker::PerformanceMetrics DataProcessingWorker::getPerformanceMetrics() const {
    PerformanceMetrics metrics;
    metrics.processedFrames = m_processedFrames.load();
    metrics.droppedFrames = m_droppedFrames.load();
    metrics.retryCount = m_retryCount.load();
    metrics.averageLatency = m_averageLatency.load();
    metrics.processingRate = m_processingRate.load();
    metrics.cpuUsage = m_cpuUsage.load();
    metrics.memoryUsage = m_memoryUsage.load();
    return metrics;
}

void DataProcessingWorker::setPerformanceThresholds(double maxLatency, double minRate) {
    m_maxLatencyThreshold = std::max(1.0, maxLatency);
    m_minRateThreshold = std::max(0.1, minRate);
    qCDebug(lcDataProcessingWorker) << "性能阈值更新，最大延迟:" << m_maxLatencyThreshold
        << "ms，最小速率:" << m_minRateThreshold << "fps";
}

bool DataProcessingWorker::validateFrame(const CapturedFrame& frame) const {
    if ( !frame.isValid() ) {
        return false;
    }

    // 检查帧延迟是否过高
    qint64 latency = frame.getLatency();
    if ( latency > m_processingTimeout ) {
        qCWarning(lcDataProcessingWorker) << "帧延迟过高:" << latency << "ms，超时阈值:" << m_processingTimeout << "ms";
        return false;
    }

    // 检查图像尺寸是否合理
    QSize size = frame.image.size();
    if ( size.width() <= 0 || size.height() <= 0 ||
        size.width() > 8192 || size.height() > 8192 ) {
        qCWarning(lcDataProcessingWorker) << "图像尺寸不合理:" << size;
        return false;
    }

    return true;
}

void DataProcessingWorker::updateProcessingStats(qint64 processingTime, bool success) {
    Q_UNUSED(success)
        m_totalProcessingTime += processingTime;

    // 计算平均延迟
    quint64 totalFrames = m_processedFrames.load() + m_droppedFrames.load();
    if ( totalFrames > 0 ) {
        m_averageLatency = static_cast<double>(m_totalProcessingTime.load()) / totalFrames;
    }
}

void DataProcessingWorker::updateStats() {
    qint64 currentTime = m_performanceTimer.elapsed();
    qint64 elapsed = currentTime - m_lastStatsUpdate;

    if ( elapsed > 0 ) {
        // 计算处理速率
        quint64 processedFrames = m_processedFrames.load();
        m_processingRate = static_cast<double>(processedFrames) / (elapsed / 1000.0);

        // 发射统计更新信号
        emit processingStatsUpdated(processedFrames, m_droppedFrames.load(),
            m_averageLatency.load(), m_processingRate.load());

        // 检查性能
        checkPerformance();

        m_lastStatsUpdate = currentTime;
    }
}

void DataProcessingWorker::checkPerformance() {
    double avgLatency = m_averageLatency.load();
    double processingRate = m_processingRate.load();

    // 检查处理延迟
    if ( avgLatency > MAX_PROCESSING_LATENCY ) {
        QString warning = QString("处理延迟过高: %1ms").arg(avgLatency, 0, 'f', 2);
        emit processingWarning(warning);
    }

    // 检查处理速率
    if ( processingRate < MIN_PROCESSING_RATE && m_processedFrames.load() > 10 ) {
        QString warning = QString("处理速率过低: %1fps").arg(processingRate, 0, 'f', 2);
        emit processingWarning(warning);
    }
}

void DataProcessingWorker::onQueueWarning(QueueManager::QueueType type, const QString& message) {
    if ( type == QueueManager::CaptureQueue || type == QueueManager::ProcessedQueue ) {
        qCWarning(lcDataProcessingWorker) << "队列警告:" << message;
        emit processingWarning(message);
    }
}

void DataProcessingWorker::onQueueError(QueueManager::QueueType type, const QString& error) {
    if ( type == QueueManager::CaptureQueue || type == QueueManager::ProcessedQueue ) {
        qCCritical(lcDataProcessingWorker) << "队列错误:" << error;
        emit processingError(error);
    }
}

void DataProcessingWorker::stopProcessingAndClearQueues() {
    qCDebug(lcDataProcessingWorker) << "停止数据处理并清空队列";

    // 立即设置停止标志，确保processTask()能快速退出
    if ( isRunning() ) {
        // 通过调用基类stop()方法设置停止请求标志，使shouldStop()返回true
        Worker::stop(false); // false表示不等待完成，立即设置停止标志
        qCDebug(lcDataProcessingWorker) << "已设置停止标志，暂停数据处理任务";
    }

    // 清空队列
    if ( m_captureQueue ) {
        int captureQueueSize = m_captureQueue->size();
        m_captureQueue->clear();
        qCDebug(lcDataProcessingWorker) << "清空捕获队列，原大小:" << captureQueueSize;
    }

    if ( m_processedQueue ) {
        int processedQueueSize = m_processedQueue->size();
        m_processedQueue->clear();
        qCDebug(lcDataProcessingWorker) << "清空处理队列，原大小:" << processedQueueSize;
    }

    // 重置统计信息
    {
        QMutexLocker locker(&m_statsMutex);
        m_processedFrames = 0;
        m_droppedFrames = 0;
        m_totalProcessingTime = 0;
        m_averageLatency = 0.0;
        m_processingRate = 0.0;
        m_retryCount = 0;
        m_cpuUsage = 0.0;
        m_memoryUsage = 0.0;

        qCDebug(lcDataProcessingWorker) << "重置统计信息完成";
    }

    // 发出统计更新信号
    emit processingStatsUpdated(0, 0, 0.0, 0.0);

    qCDebug(lcDataProcessingWorker) << "停止数据处理并清空队列完成";
}

void DataProcessingWorker::resumeProcessing() {
    qCDebug(lcDataProcessingWorker) << "恢复数据处理";

    // 确保工作线程正在运行
    if ( !isRunning() ) {
        qCWarning(lcDataProcessingWorker) << "工作线程未运行，无法恢复处理";
        return;
    }

    // 重新启动统计定时器
    if ( m_statsTimer && !m_statsTimer->isActive() ) {
        m_statsTimer->start(m_statsUpdateInterval);
        qCDebug(lcDataProcessingWorker) << "重新启动统计定时器";
    }

    // 重新启动资源监控定时器
    if ( m_resourceMonitorTimer && !m_resourceMonitorTimer->isActive() ) {
        m_resourceMonitorTimer->start(5000); // 每5秒监控一次
        qCDebug(lcDataProcessingWorker) << "重新启动资源监控定时器";
    }

    // 重新启动自适应调整定时器
    if ( m_adaptiveTimer && m_adaptiveMode && !m_adaptiveTimer->isActive() ) {
        m_adaptiveTimer->start(10000); // 每10秒调整一次
        qCDebug(lcDataProcessingWorker) << "重新启动自适应调整定时器";
    }

    qCDebug(lcDataProcessingWorker) << "恢复数据处理完成";
}
