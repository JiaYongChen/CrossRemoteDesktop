#include "DataProcessingWorker.h"
#include "../../common/logging/LoggingCategories.h"
#include "../../common/config/ProcessingConstants.h"
#include <QtCore/QThread>
#include <turbojpeg.h>
#include <QtConcurrent/QtConcurrent>


DataProcessingWorker::DataProcessingWorker(QObject* parent)
    : Worker(parent)
    , m_captureQueue(nullptr)
    , m_processedQueue(nullptr)
    , m_processingTimeout(ProcessingConstants::DefaultProcessingTimeoutMs) {
    qCDebug(lcServerEncode) << "DataProcessingWorker 构造函数";
}

DataProcessingWorker::~DataProcessingWorker() {
    qCDebug(lcServerEncode) << "DataProcessingWorker析构函数";
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

void DataProcessingWorker::setQueues(ThreadSafeQueue<CapturedFrame>* captureQueue,
                                     ThreadSafeQueue<ProcessedData>* processedQueue) {
    m_captureQueue = captureQueue;
    m_processedQueue = processedQueue;
}

void DataProcessingWorker::setJpegQuality(int quality) {
    m_jpegQuality.store(qBound(1, quality, 100), std::memory_order_relaxed);
}

int DataProcessingWorker::jpegQuality() const {
    return m_jpegQuality.load(std::memory_order_relaxed);
}

void DataProcessingWorker::setChromaSubsampling(int colorDepth) {
    int samp;
    switch (colorDepth) {
        case 32: samp = TJSAMP_444; break;
        case 24: samp = TJSAMP_422; break;
        case 16: samp = TJSAMP_420; break;
        default:
            samp = TJSAMP_420;
            qCWarning(lcServerEncode) << "非预期的色深值:" << colorDepth
                                      << "— 回退为 TJSAMP_420 (4:2:0)";
            break;
    }
    m_chromaSubsampling.store(samp, std::memory_order_relaxed);
    qCDebug(lcServerEncode) << "色度子采样策略更新: colorDepth=" << colorDepth
                            << "TJSAMP=" << samp;
}

void DataProcessingWorker::setProcessingTimeout(int timeoutMs) {
    qCDebug(lcServerEncode) << "设置处理超时时间:" << timeoutMs << "毫秒";
    m_processingTimeout = timeoutMs;
}

bool DataProcessingWorker::initialize() {
    qCDebug(lcServerEncode) << "初始化 DataProcessingWorker";

    try {
        if ( !m_captureQueue || !m_processedQueue ) {
            qCCritical(lcServerEncode) << "未设置队列指针";
            return false;
        }

        // 创建异步编码 Watcher（由本线程事件循环驱动）
        m_asyncWatcher = new QFutureWatcher<ProcessedData>(this);
        connect(m_asyncWatcher, &QFutureWatcher<ProcessedData>::finished,
                this, &DataProcessingWorker::onAsyncBatchFinished);

        qCInfo(lcServerEncode) << "DataProcessingWorker 初始化成功";
        return true;

    } catch ( const std::exception& e ) {
        qCCritical(lcServerEncode) << "初始化异常:" << e.what();
        return false;
    } catch ( ... ) {
        qCCritical(lcServerEncode) << "初始化未知异常";
        return false;
    }
}

void DataProcessingWorker::stop(bool waitForFinish) {
    qCInfo(lcServerEncode) << "停止DataProcessingWorker";

    // 调用父类的stop方法
    Worker::stop(waitForFinish);
}

void DataProcessingWorker::cleanup() {
    qCDebug(lcServerEncode) << "清理DataProcessingWorker";

    // 停止处理并清空队列，确保processTask能快速退出
    stopProcessingAndClearQueues();
    qCDebug(lcServerEncode) << "已停止处理并清空队列";

    // 重置队列指针
    m_captureQueue = nullptr;
    m_processedQueue = nullptr;

    Worker::cleanup();
    qCInfo(lcServerEncode) << "DataProcessingWorker清理完成";
}

void DataProcessingWorker::processTask() {
    // 首先检查是否应该停止
    if ( shouldStop() ) {
        qCDebug(lcServerEncode) << "检测到停止信号，退出processTask";
        return;
    }

    if ( !m_captureQueue || !m_processedQueue ) {
        return;
    }

    // 非阻塞模式：有飞行中的编码批次时直接返回，由 Worker 的事件循环
    // 在下一次 processTask 中重新检查。避免 waitForFinished 阻断线程。
    if ( m_inFlightBatches.load() >= ProcessingConstants::MaxInFlightBatches ) {
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

        // 第一次获取：从捕获队列出队
        if ( m_captureQueue->tryDequeue(firstFrame) ) {
            // 获取到数据后再次检查停止状态
            if ( shouldStop() ) {
                qCDebug(lcServerEncode) << "获取帧数据后检测到停止信号，退出处理";
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
                if ( !m_captureQueue->tryDequeue(additionalFrame) ) {
                    // 队列为空，退出收集
                    break;
                }
                frameBatch.push_back(std::move(additionalFrame));

                // 检查是否需要停止
                if ( shouldStop() ) {
                    qCDebug(lcServerEncode) << "检测到停止信号，退出批量收集";
                    break;
                }
            }

            // 异步非阻塞提交：编码在线程池中执行，本线程不等待
            if ( !frameBatch.empty() ) {
                processBatchAsync(std::move(frameBatch));
            }
        }

    } catch ( const std::exception& e ) {
        qCCritical(lcServerEncode) << "processTask异常:" << e.what();

        // 异常后短暂休眠，避免连续异常导致CPU占用过高
        QThread::msleep(10);
    } catch ( ... ) {
        qCCritical(lcServerEncode) << "processTask未知异常";

        // 异常后短暂休眠，避免连续异常导致CPU占用过高
        QThread::msleep(10);
    }
}

void DataProcessingWorker::processBatchAsync(std::vector<CapturedFrame>&& frames) {
    const int currentQuality = m_jpegQuality.load(std::memory_order_relaxed);
    const int currentSubsampling = m_chromaSubsampling.load(std::memory_order_relaxed);
    const double currentScale = ProcessingConstants::ScaleFactorHigh;

    // 将帧存入 shared_ptr，确保线程池异步编码期间不会被销毁
    auto sharedFrames = std::make_shared<std::vector<CapturedFrame>>();
    sharedFrames->reserve(frames.size());

    for ( auto& frame : frames ) {
        if ( !frame.isValid() ) { continue; }
        if ( frame.getLatency() > m_processingTimeout ) { continue; }
        // 防止超大尺寸帧进入编码器导致 OOM（宽或高超过 8192 像素视为异常）
        if ( frame.image ) {
            const QSize sz = frame.image->size();
            if ( sz.width() > 8192 || sz.height() > 8192 ) {
                qCWarning(lcServerEncode) << "帧尺寸异常，丢弃:"
                    << sz.width() << "x" << sz.height()
                    << "帧ID:" << frame.frameId;
                continue;
            }
        }
        sharedFrames->push_back(std::move(frame));
    }

    if ( sharedFrames->empty() ) {
        return;
    }

    // 保持飞行中批次引用，防止在编码完成前被清理
    m_inFlightFrames = sharedFrames;
    m_inFlightBatches.fetch_add(1);

    // Lambda 按值捕获 shared_ptr —— 帧数据生命周期与异步任务绑定
    QFuture<ProcessedData> future = QtConcurrent::mapped(*sharedFrames,
        [currentQuality, currentScale, currentSubsampling, sharedFrames](const CapturedFrame& frame) -> ProcessedData {
        auto pd = DataProcessingWorker::encodeImageParallel(
            *frame.image, frame.frameId, currentQuality, currentScale, currentSubsampling);
        pd.captureTimestamp = static_cast<quint64>(frame.timestamp.toMSecsSinceEpoch());
        return pd;
    });

    m_asyncWatcher->setFuture(future);
}

void DataProcessingWorker::onAsyncBatchFinished() {
    if ( shouldStop() ) return;

    const QFuture<ProcessedData>& future = m_asyncWatcher->future();
    const QList<ProcessedData> results = future.results();

    for ( const auto& pd : results ) {
        if ( pd.isValid() && m_processedQueue ) {
            m_processedQueue->tryEnqueueDrainToLatest(pd);
        }
    }

    // 释放帧数据引用，下一轮 processTask 可提交新批次
    m_inFlightFrames.reset();
    m_inFlightBatches.fetch_sub(1);
}

ProcessedData DataProcessingWorker::encodeImageParallel(const QImage& image, quint64 frameId,
                                                        int quality, double scaleFactor,
                                                        int chromaSubsampling) {
    ProcessedData result;

    try {
        // 验证输入图像
        if ( image.isNull() || image.size().isEmpty() ) {
            qCWarning(lcServerEncode) << "输入图像无效，帧ID:" << frameId
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
                qCWarning(lcServerEncode) << "图像缩放失败，帧ID:" << frameId;
                workingImage = image; // 回退到原始图像
            }
        }

        // 转换为 RGB888 格式（turbojpeg 原生 TJPF_RGB）
        QImage convertedImage = workingImage;
        if ( workingImage.format() != QImage::Format_RGB888 ) {
            convertedImage = workingImage.convertToFormat(QImage::Format_RGB888);
            if ( convertedImage.isNull() ) {
                qCWarning(lcServerEncode) << "图像格式转换失败，帧ID:" << frameId;
                return result;
            }
        }

        // 每 100 帧输出一次编码信息，避免刷屏
        if ( frameId <= 3 || frameId % 100 == 0 ) {
            qCDebug(lcServerEncode) << "编码JPEG(turbo)，帧ID:" << frameId
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
                qCWarning(lcServerEncode) << "tjInitCompress 失败，帧ID:" << frameId;
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
            chromaSubsampling,  // 色度子采样策略（由客户端色深偏好驱动）
            quality,
            TJFLAG_FASTDCT);  // 快速 DCT 算法

        if ( tjRet != 0 ) {
            qCWarning(lcServerEncode) << "tjCompress2 失败:" << tjGetErrorStr2(tjCompress)
                                               << "帧ID:" << frameId;
            return result;
        }

        QByteArray jpegData(reinterpret_cast<const char*>(jpegBuf), static_cast<int>(jpegSize));
        tjFree(jpegBuf);

        if ( jpegData.isEmpty() ) {
            qCWarning(lcServerEncode) << "JPEG编码结果为空，帧ID:" << frameId;
            return result;
        }

        // 判断是否进行了缩放
        bool wasScaled = (scaleFactor < 1.0 && scaleFactor > 0.1);

        // 每 100 帧输出一次压缩统计，避免刷屏
        if ( frameId <= 3 || frameId % 100 == 0 ) {
            qCDebug(lcServerEncode) << "帧ID:" << frameId
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
        qCCritical(lcServerEncode) << "图像处理异常:" << e.what() << "帧ID:" << frameId;
    } catch ( ... ) {
        qCCritical(lcServerEncode) << "图像处理未知异常，帧ID:" << frameId;
    }

    return result;
}

void DataProcessingWorker::stopProcessingAndClearQueues() {
    qCDebug(lcServerEncode) << "停止数据处理并清空队列";

    // 立即设置停止标志，确保processTask()能快速退出
    if ( isRunning() ) {
        Worker::stop(false);
        qCDebug(lcServerEncode) << "已设置停止标志，暂停数据处理任务";
    }

    // 取消飞行中的异步编码批次
    if ( m_asyncWatcher && m_asyncWatcher->isRunning() ) {
        m_asyncWatcher->cancel();
        m_asyncWatcher->waitForFinished();
        m_inFlightFrames.reset();
        m_inFlightBatches.store(0);
        qCDebug(lcServerEncode) << "已等待飞行中的异步编码批次完成";
    }

    // 清空队列
    if ( m_captureQueue ) {
        m_captureQueue->clear();
    }
    if ( m_processedQueue ) {
        m_processedQueue->clear();
    }

    qCDebug(lcServerEncode) << "停止数据处理并清空队列完成";
}
