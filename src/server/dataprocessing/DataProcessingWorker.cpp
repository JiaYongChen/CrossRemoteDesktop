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
    } else {
        // 线程已停止或在当前线程，直接清理
        // 注意：不要在线程停止后尝试 moveToThread，这会导致警告
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
        // 保存队列管理器指针，便于后续使用统一接口和断开信号连接
        m_queueManager = queueManager;

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
    qCDebug(lcDataProcessingWorker) << "停止DataProcessingWorker";

    // 调用父类的stop方法
    Worker::stop(waitForFinish);
}

void DataProcessingWorker::cleanup() {
    qCDebug(lcDataProcessingWorker) << "清理DataProcessingWorker";

    // 停止处理并清空队列，确保processTask能快速退出
    stopProcessingAndClearQueues();
    qCDebug(lcDataProcessingWorker) << "已停止处理并清空队列";

    // 停止所有定时器
    if ( m_statsTimer && m_statsTimer->isActive() ) {
        m_statsTimer->stop();
        qCDebug(lcDataProcessingWorker) << "统计定时器已停止";
    }

    // 断开队列管理器信号连接
    if ( m_queueManager ) {
        disconnect(m_queueManager, nullptr, this, nullptr);
    }

    // 清理数据处理器
    m_dataProcessor.reset();

    // 重置队列管理器引用
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

    if ( !m_queueManager ) {
        return;
    }

    try {
        // 批量处理帧数据以提高效率 - 动态调整批大小
        const int maxBatchSize = std::min(m_maxParallelTasks * 2, 10); // 最多10帧
        std::vector<CapturedFrame> frameBatch;
        frameBatch.reserve(maxBatchSize);

        // 自动处理机制：使用带超时的阻塞式获取第一帧数据
        CapturedFrame firstFrame;
        bool hasFirstFrame = false;

        // 第一次获取使用带超时的阻塞方式，实现自动处理
        // 使用 QueueManager 统一接口出队
        if ( m_queueManager->dequeueCapturedFrame(firstFrame) ) {
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
                // 使用 QueueManager 统一接口尝试出队
                if ( !m_queueManager->dequeueCapturedFrame(additionalFrame) ) {
                    // 队列为空，退出收集
                    break;
                }
                frameBatch.push_back(std::move(additionalFrame));

                // 检查是否需要停止
                if ( shouldStop() ) {
                    qCDebug(lcDataProcessingWorker) << "检测到停止信号，退出批量收集";
                    break;
                }
            }

            // 调用统一的异步批处理方法
            if ( !frameBatch.empty() ) {
                processBatchParallel(frameBatch);
            }
        }

        // 定期检查系统资源和性能
        static int taskCount = 0;
        if ( ++taskCount % 50 == 0 ) { // 每50次任务检查一次
            // 在检查系统资源前也要确认没有停止信号
            if ( shouldStop() ) {
                qCDebug(lcDataProcessingWorker) << "检测到停止信号，跳过性能检查";
                return;
            }

            checkPerformance();
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
            // 使用 QueueManager 统一接口入队
            if ( m_queueManager && m_queueManager->enqueueProcessedData(processedData) ) {
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
        // 确保图像格式为 RGB888，这是 JPEG 格式推荐的格式
        QImage convertedImage = image;
        if ( image.format() != QImage::Format_RGB888 && image.format() != QImage::Format_RGB32 ) {
            convertedImage = image.convertToFormat(QImage::Format_RGB888);
        }

        // 使用 QBuffer 将图像编码为 JPEG 格式
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);

        // 保存为 JPEG 格式，质量设置为 85（推荐值）
        // JPEG 是有损压缩格式，quality 范围 0-100
        // 85 提供良好的压缩率和视觉质量平衡
        int quality = 85;
        if ( !convertedImage.save(&buffer, "JPEG", quality) ) {
            qCWarning(lcDataProcessingWorker) << "无法将图像编码为JPEG格式，帧ID:" << frameId;
            return result;
        }

        buffer.close();
        QByteArray jpegData = buffer.data();

        if ( jpegData.isEmpty() ) {
            qCWarning(lcDataProcessingWorker) << "JPEG编码结果为空，帧ID:" << frameId;
            return result;
        }

        // 构造ProcessedData
        result.originalFrameId = frameId;
        result.compressedData = jpegData;
        result.imageSize = convertedImage.size();
        result.processedTime = QDateTime::currentDateTime();
        result.originalDataSize = convertedImage.sizeInBytes();
        result.compressedDataSize = jpegData.size();
    } catch ( const std::exception& e ) {
        qCCritical(lcDataProcessingWorker) << "图像处理异常:" << e.what() << "帧ID:" << frameId;
    } catch ( ... ) {
        qCCritical(lcDataProcessingWorker) << "图像处理未知异常，帧ID:" << frameId;
    }

    return result;
}

DataProcessingWorker::PerformanceMetrics DataProcessingWorker::getPerformanceMetrics() const {
    PerformanceMetrics metrics;
    metrics.processedFrames = m_processedFrames.load();
    metrics.droppedFrames = m_droppedFrames.load();
    metrics.averageLatency = m_averageLatency.load();
    metrics.processingRate = m_processingRate.load();
    return metrics;
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

    // 使用 QueueManager 统一接口清空队列
    if ( m_queueManager ) {
        m_queueManager->clearQueue(QueueManager::CaptureQueue);
        m_queueManager->clearQueue(QueueManager::ProcessedQueue);
        qCDebug(lcDataProcessingWorker) << "已清空捕获队列和处理队列";
    }

    // 重置统计信息
    {
        QMutexLocker locker(&m_statsMutex);
        m_processedFrames = 0;
        m_droppedFrames = 0;
        m_totalProcessingTime = 0;
        m_averageLatency = 0.0;
        m_processingRate = 0.0;

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

    qCDebug(lcDataProcessingWorker) << "恢复数据处理完成";
}
