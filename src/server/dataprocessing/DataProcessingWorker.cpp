#include "DataProcessingWorker.h"
#include "../../common/core/logging/LoggingCategories.h"
#include "../../common/core/config/Constants.h"
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>
#include <QtCore/QIODevice>
#include <QtCore/QBuffer>
#include <QtGui/QImageWriter>
#include <algorithm>


DataProcessingWorker::DataProcessingWorker(QObject* parent)
    : Worker(parent)
    , m_queueManager(nullptr)
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
    qCInfo(lcDataProcessingWorker) << "最大并行处理线程数:" << m_maxParallelTasks;
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

void DataProcessingWorker::setProcessingTimeout(int timeoutMs) {
    qCDebug(lcDataProcessingWorker) << "设置处理超时时间:" << timeoutMs << "毫秒";
    m_processingTimeout = timeoutMs;
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
        // 保存队列管理器指针
        m_queueManager = queueManager;

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

    // 飞行任务限流：阻止线程池任务队列无界增长导致延迟累积。
    // 活跃编码任务数达到上限时暂停提交，等 lambda 完成后递减再继续。
    if ( m_activeParallelTasks.load() >= m_maxParallelTasks ) {
        setDidWork(false);
        return;
    }

    // 流水池背压：PQ 满时暂停消费 CQ。
    if ( m_queueManager && m_queueManager->isProcessedQueueFull() ) {
        qCDebug(lcDataProcessingWorker) << "ProcessedQueue 满 ("
                                        << m_queueManager->getProcessedQueueSize()
                                        << ")，暂停编码等待发送端消化";
        setDidWork(false);
        return;
    }

    try {
        // 动态批次大小：由硬上限、可用CPU线程数、CQ当前帧数、PQ剩余容量共同决定。
        const int maxBatchSize = 1;
        const int cqSize = m_queueManager->getCaptureQueueSize();
        const int pqMaxSize = m_queueManager->getProcessedQueueMaxSize();
        const int pqSize = m_queueManager->getProcessedQueueSize();
        const int pqRemaining = pqMaxSize - pqSize;
        const int effectiveBatchSize = std::max(1, std::min({maxBatchSize,
                                                             m_maxParallelTasks,
                                                             cqSize,
                                                             pqRemaining}));

        std::vector<CapturedFrame> frameBatch;
        frameBatch.reserve(static_cast<size_t>(effectiveBatchSize));

        CapturedFrame firstFrame;
        if ( m_queueManager->dequeueCapturedFrame(firstFrame) ) {
            if ( shouldStop() ) return;
            frameBatch.push_back(std::move(firstFrame));
        }

        setDidWork(!frameBatch.empty());

        while ( frameBatch.size() < static_cast<size_t>(effectiveBatchSize) ) {
            CapturedFrame additionalFrame;
            if ( !m_queueManager->dequeueCapturedFrame(additionalFrame) ) break;
            frameBatch.push_back(std::move(additionalFrame));
            if ( shouldStop() ) break;
        }

        // 火后不管：提交到线程池异步编码，不跟踪不回调，lambda 直接入队 PQ。
        if ( !frameBatch.empty() ) {
            const int quality = CoreConstants::Compression::DEFAULT_WEBP_QUALITY;
            const double scale = CoreConstants::Compression::SCALE_FACTOR_HIGH;

            for ( auto& f : frameBatch ) {
                if ( !f.isValid() || f.getLatency() > 5000 ) { ++m_droppedFrames; continue; }

                auto capturedImage = f.image;
                auto frameId       = f.frameId;
                auto ts            = f.timestamp;

                // 统一全帧 WebP 编码
                m_activeParallelTasks.fetch_add(1);
                QThreadPool::globalInstance()->start([quality, scale, capturedImage,
                    frameId, ts, this]() {
                    auto pd = encodeImage(*capturedImage, frameId, quality, scale);
                    pd.captureTimestamp = static_cast<quint64>(ts.toMSecsSinceEpoch());

                    if ( pd.isValid() && m_queueManager &&
                         m_queueManager->enqueueProcessedData(pd) ) {
                        m_processedFrames++;
                    } else {
                        m_droppedFrames++;
                    }
                    m_activeParallelTasks.fetch_sub(1);
                });
            }
        }

        // 定期检查系统资源和性能
        static int taskCount = 0;
        if ( ++taskCount % 50 == 0 ) {
            if ( shouldStop() ) return;
            checkPerformance();
        }

    } catch ( const std::exception& e ) {
        qCCritical(lcDataProcessingWorker) << "processTask异常:" << e.what();

        // 异常后短暂休眠，避免连续异常导致CPU占用过高
        QThread::msleep(10);
    } catch ( ... ) {
        qCCritical(lcDataProcessingWorker) << "processTask未知异常";

        // 异常后短暂休眠，避免连续异常导致CPU占用过高
        QThread::msleep(10);
    }
}

ProcessedData DataProcessingWorker::encodeImage(const QImage& image, quint64 frameId,
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

        // 确保图像格式为 RGB888，这是 WebP 编码推荐的格式
        // 在Windows下，Format_RGB32 更常用且兼容性更好
        QImage convertedImage = workingImage;
        if ( workingImage.format() != QImage::Format_RGB32 && workingImage.format() != QImage::Format_RGB888 ) {
            qCDebug(lcDataProcessingWorker) << "转换图像格式，原格式:" << workingImage.format() 
                << "目标格式: RGB32，帧ID:" << frameId;
            convertedImage = workingImage.convertToFormat(QImage::Format_RGB32);
            
            if ( convertedImage.isNull() ) {
                qCWarning(lcDataProcessingWorker) << "图像格式转换失败，帧ID:" << frameId;
                return result;
            }
        }

        // 使用 QBuffer 将图像编码为 WebP 格式
        QByteArray encodedData;
        QBuffer buffer(&encodedData);
        
        if ( !buffer.open(QIODevice::WriteOnly) ) {
            qCWarning(lcDataProcessingWorker) << "无法打开QBuffer，帧ID:" << frameId;
            return result;
        }

        // 每 100 帧输出一次编码信息，避免刷屏
        if ( frameId <= 3 || frameId % 100 == 0 ) {
            qCDebug(lcDataProcessingWorker) << "编码帧，格式:WebP 帧ID:" << frameId
                << "原始尺寸:" << image.size()
                << "处理后尺寸:" << convertedImage.size()
                << "缩放因子:" << scaleFactor
                << "质量:" << quality;
        }

        // WebP 编码（Qt 6 内置，跨平台零依赖）
        bool saveSuccess = convertedImage.save(&buffer, "WEBP", quality);

        if ( !saveSuccess ) {
            // 第一次诊断输出，记录更详细的错误信息
            static bool diagnosticPrinted = false;
            if ( !diagnosticPrinted ) {
                qCWarning(lcDataProcessingWorker) << "WebP 编码失败诊断信息:";
                qCWarning(lcDataProcessingWorker) << "  图像尺寸:" << convertedImage.size();
                qCWarning(lcDataProcessingWorker) << "  图像格式:" << convertedImage.format();
                qCWarning(lcDataProcessingWorker) << "  支持的图像格式:"
                    << QImageWriter::supportedImageFormats();
                diagnosticPrinted = true;
            }

            qCWarning(lcDataProcessingWorker) << "无法将图像编码为 WebP 格式，帧ID:" << frameId
                << "图像尺寸:" << convertedImage.size() << "格式:" << convertedImage.format();
            return result;
        }

        if ( encodedData.isEmpty() ) {
            qCWarning(lcDataProcessingWorker) << "编码结果为空，帧ID:" << frameId;
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
                << "最终:" << encodedData.size() << "字节";
        }

        // 构造ProcessedData
        result.originalFrameId = frameId;
        result.compressedData = encodedData;
        result.imageSize = convertedImage.size();         // 当前图像尺寸（可能是缩放后的）
        result.originalImageSize = image.size();          // 原始图像尺寸
        result.processedTime = QDateTime::currentDateTime();
        result.originalDataSize = image.sizeInBytes();    // 原始图像数据大小
        result.compressedDataSize = encodedData.size();
        result.isScaled = wasScaled;                      // 标记是否进行了缩放
    } catch ( const std::exception& e ) {
        qCCritical(lcDataProcessingWorker) << "图像处理异常:" << e.what() << "帧ID:" << frameId;
    } catch ( ... ) {
        qCCritical(lcDataProcessingWorker) << "图像处理未知异常，帧ID:" << frameId;
    }

    return result;
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
        qCWarning(lcDataProcessingWorker) << "处理延迟过高:" << QString::number(avgLatency, 'f', 2) << "ms";
    }

    // 检查处理速率
    if ( processingRate < MIN_PROCESSING_RATE && m_processedFrames.load() > 10 ) {
        qCWarning(lcDataProcessingWorker) << "处理速率过低:" << QString::number(processingRate, 'f', 2) << "fps";
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

    // 火后不管模式下无法取消飞行中的编码任务——帧生命周期由
    // lambda 捕获的 shared_ptr 保证，队列清理后编码完成时入队会静默失败。

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
