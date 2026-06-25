#include "DataProcessingWorker.h"
#include "../../common/core/logging/LoggingCategories.h"
#include "../../common/core/config/Constants.h"
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>
#include <QtCore/QIODevice>
#include <QtCore/QBuffer>
#include <turbojpeg.h>
#include <QtConcurrent/QtConcurrent>
#include <cstring>
#include <algorithm>


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

void DataProcessingWorker::setJpegQuality(int quality) {
    m_jpegQuality.store(qBound(1, quality, 100), std::memory_order_relaxed);
}

int DataProcessingWorker::jpegQuality() const {
    return m_jpegQuality.load(std::memory_order_relaxed);
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

bool DataProcessingWorker::initialize() {
    qCDebug(lcDataProcessingWorker) << "初始化 DataProcessingWorker";

    try {
        if ( !m_queueManager ) {
            qCCritical(lcDataProcessingWorker) << "未设置队列管理器";
            return false;
        }

        // 连接队列信号
        connect(m_queueManager, &QueueManager::queueWarning,
            this, &DataProcessingWorker::onQueueWarning);
        connect(m_queueManager, &QueueManager::queueError,
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

        // 创建异步编码 Watcher（由本线程事件循环驱动）
        m_asyncWatcher = new QFutureWatcher<ProcessedData>(this);
        connect(m_asyncWatcher, &QFutureWatcher<ProcessedData>::finished,
                this, &DataProcessingWorker::onAsyncBatchFinished);

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

    // 非阻塞模式：有飞行中的编码批次时直接返回，由 Worker 的事件循环
    // 在下一次 processTask 中重新检查。避免 waitForFinished 阻断线程。
    if ( m_inFlightBatches.load() >= kMaxInFlightBatches ) {
        setDidWork(false);  // 告知 workLoop 本回合空闲，应进入 1ms 睡眠
        return;
    }

    try {
        // Drain-to-latest: dequeueCapturedFrame 清空队列并返回最新帧，
        // 因此每轮只需出队一帧。编码线程数沿用全核配置（单帧编码一个核）。
        const int maxBatchSize = 1;
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

        // Adaptive sleep: skip idle sleep when processing frames
        setDidWork(hasFirstFrame);

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

            // 异步非阻塞提交：编码在线程池中执行，本线程不等待
            if ( !frameBatch.empty() ) {
                processBatchAsync(std::move(frameBatch));
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
        emit processingError(RdError(ErrorCode::DataProcessingException, QString("数据处理任务异常: %1").arg(e.what()), "DataProcessingWorker"));

        // 异常后短暂休眠，避免连续异常导致CPU占用过高
        QThread::msleep(10);
    } catch ( ... ) {
        qCCritical(lcDataProcessingWorker) << "processTask未知异常";
        emit processingError(RdError(ErrorCode::DataProcessingException, "数据处理任务发生未知异常", "DataProcessingWorker"));

        // 异常后短暂休眠，避免连续异常导致CPU占用过高
        QThread::msleep(10);
    }
}

void DataProcessingWorker::processBatchAsync(std::vector<CapturedFrame>&& frames) {
    const int currentQuality = m_jpegQuality.load(std::memory_order_relaxed);
    const double currentScale = CoreConstants::Compression::SCALE_FACTOR_HIGH;

    // 将帧存入 shared_ptr，确保线程池异步编码期间不会被销毁
    auto sharedFrames = std::make_shared<std::vector<CapturedFrame>>();
    sharedFrames->reserve(frames.size());

    int droppedCount = 0;
    for ( auto& frame : frames ) {
        if ( !frame.isValid() ) { ++droppedCount; continue; }
        if ( frame.getLatency() > 5000 ) { ++droppedCount; continue; }
        sharedFrames->push_back(std::move(frame));
    }

    m_droppedFrames += droppedCount;

    if ( sharedFrames->empty() ) {
        return;
    }

    // 保持飞行中批次引用，防止在编码完成前被清理
    m_inFlightFrames = sharedFrames;
    m_inFlightBatches.fetch_add(1);

    // Lambda 按值捕获 shared_ptr —— 帧数据生命周期与异步任务绑定
    QFuture<ProcessedData> future = QtConcurrent::mapped(*sharedFrames,
        [currentQuality, currentScale, sharedFrames](const CapturedFrame& frame) -> ProcessedData {
        auto pd = DataProcessingWorker::encodeImageParallel(
            *frame.image, frame.frameId, currentQuality, currentScale);
        pd.captureTimestamp = static_cast<quint64>(frame.timestamp.toMSecsSinceEpoch());
        return pd;
    });

    m_asyncWatcher->setFuture(future);
}

void DataProcessingWorker::onAsyncBatchFinished() {
    const QFuture<ProcessedData>& future = m_asyncWatcher->future();
    const QList<ProcessedData> results = future.results();

    int successCount = 0;
    int droppedCount = 0;

    for ( const auto& pd : results ) {
        if ( pd.isValid() ) {
            if ( m_queueManager && m_queueManager->enqueueProcessedData(pd) ) {
                ++successCount;
                m_processedFrames++;
            } else {
                ++droppedCount;
                m_droppedFrames++;
            }
        } else {
            ++droppedCount;
            m_droppedFrames++;
        }
    }

    // 释放帧数据引用，下一轮 processTask 可提交新批次
    m_inFlightFrames.reset();
    m_inFlightBatches.fetch_sub(1);
}

ProcessedData DataProcessingWorker::encodeImageParallel(const QImage& image, quint64 frameId,
                                                        int quality, double scaleFactor) {
    ProcessedData result;

    try {
        // 验证输入图像
        if ( image.isNull() || image.size().isEmpty() ) {
            qCWarning(lcDataProcessingWorker) << "输入图像无效，帧ID:" << frameId
                << "isNull:" << image.isNull() << "size:" << image.size();
            return result;
        }

        // 应用缩放（如果缩放因子不为1.0）
        QImage workingImage = image;
        if ( scaleFactor < 1.0 && scaleFactor > 0.1 ) {
            int newWidth = static_cast<int>(image.width() * scaleFactor);
            int newHeight = static_cast<int>(image.height() * scaleFactor);
            workingImage = image.scaled(newWidth, newHeight, Qt::KeepAspectRatio, 
                                        Qt::FastTransformation);
            if ( workingImage.isNull() ) {
                qCWarning(lcDataProcessingWorker) << "图像缩放失败，帧ID:" << frameId;
                workingImage = image; // 回退到原始图像
            }
        }

        // 转换为 RGB888 格式（turbojpeg 原生 TJPF_RGB）
        QImage convertedImage = workingImage;
        if ( workingImage.format() != QImage::Format_RGB888 ) {
            convertedImage = workingImage.convertToFormat(QImage::Format_RGB888);
            if ( convertedImage.isNull() ) {
                qCWarning(lcDataProcessingWorker) << "图像格式转换失败，帧ID:" << frameId;
                return result;
            }
        }

        // 每 100 帧输出一次编码信息，避免刷屏
        if ( frameId <= 3 || frameId % 100 == 0 ) {
            qCDebug(lcDataProcessingWorker) << "编码JPEG(turbo)，帧ID:" << frameId
                << "原始尺寸:" << image.size()
                << "处理后尺寸:" << convertedImage.size()
                << "缩放因子:" << scaleFactor
                << "质量:" << quality;
        }

        // 线程局部 turbojpeg 压缩器句柄（每个 QtConcurrent 工作线程一个，复用）
        thread_local tjhandle tjCompress = nullptr;
        if ( !tjCompress ) {
            tjCompress = tjInitCompress();
            if ( !tjCompress ) {
                qCWarning(lcDataProcessingWorker) << "tjInitCompress 失败，帧ID:" << frameId;
                return result;
            }
        }

        unsigned char* jpegBuf = nullptr;
        unsigned long jpegSize = 0;
        const int tjRet = tjCompress2(tjCompress,
            convertedImage.constBits(),
            convertedImage.width(),
            0,  // pitch（0 = width × pixelSize，RGB888 自动计算）
            convertedImage.height(),
            TJPF_RGB,
            &jpegBuf, &jpegSize,
            TJSAMP_420,       // 4:2:0 色度子采样——视觉无差异，体积 -55%
            quality,
            TJFLAG_FASTDCT);  // 快速 DCT 算法

        if ( tjRet != 0 ) {
            qCWarning(lcDataProcessingWorker) << "tjCompress2 失败:" << tjGetErrorStr2(tjCompress)
                                               << "帧ID:" << frameId;
            return result;
        }

        QByteArray jpegData(reinterpret_cast<const char*>(jpegBuf), static_cast<int>(jpegSize));
        tjFree(jpegBuf);

        if ( jpegData.isEmpty() ) {
            qCWarning(lcDataProcessingWorker) << "JPEG编码结果为空，帧ID:" << frameId;
            return result;
        }

        // 判断是否进行了缩放
        bool wasScaled = (scaleFactor < 1.0 && scaleFactor > 0.1);

        // 每 100 帧输出一次压缩统计，避免刷屏
        if ( frameId <= 3 || frameId % 100 == 0 ) {
            qCDebug(lcDataProcessingWorker) << "帧ID:" << frameId
                << "原始图像尺寸:" << image.size()
                << "处理后尺寸:" << convertedImage.size()
                << "缩放:" << (wasScaled ? QString::number(scaleFactor) : "无")
                << "质量:" << quality
                << "最终:" << jpegData.size() << "字节";
        }

        // 构造ProcessedData
        result.originalFrameId = frameId;
        result.compressedData = jpegData;
        result.imageSize = convertedImage.size();         // 当前图像尺寸（可能是缩放后的）
        result.originalImageSize = image.size();          // 原始图像尺寸
        result.processedTime = QDateTime::currentDateTime();
        result.originalDataSize = image.sizeInBytes();    // 原始图像数据大小
        result.compressedDataSize = jpegData.size();
        result.isScaled = wasScaled;                      // 标记是否进行了缩放
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
    // frame.isValid() 已在首行校验 image 非空且非 Null，这里可安全解引用；
    // 但显式再做一次空指针校验，避免 validateFrame 被从其他路径直接调用时崩溃。
    if ( !frame.image ) {
        qCWarning(lcDataProcessingWorker) << "frame.image is null";
        return false;
    }
    QSize size = frame.image->size();
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

void DataProcessingWorker::onQueueError(const RdError& error) {
    qCCritical(lcDataProcessingWorker) << "队列错误:" << error.logLabel();
    emit processingError(RdError(ErrorCode::QueueEnqueueFailed, error.logLabel(), "DataProcessingWorker"));
}

void DataProcessingWorker::stopProcessingAndClearQueues() {
    qCDebug(lcDataProcessingWorker) << "停止数据处理并清空队列";

    // 立即设置停止标志，确保processTask()能快速退出
    if ( isRunning() ) {
        // 通过调用基类stop()方法设置停止请求标志，使shouldStop()返回true
        Worker::stop(false); // false表示不等待完成，立即设置停止标志
        qCDebug(lcDataProcessingWorker) << "已设置停止标志，暂停数据处理任务";
    }

    // 取消飞行中的异步编码批次
    if ( m_asyncWatcher && m_asyncWatcher->isRunning() ) {
        m_asyncWatcher->cancel();
        m_asyncWatcher->waitForFinished();
        m_inFlightFrames.reset();
        m_inFlightBatches.store(0);
        qCDebug(lcDataProcessingWorker) << "已等待飞行中的异步编码批次完成";
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
