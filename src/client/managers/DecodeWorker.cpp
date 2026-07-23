#include "DecodeWorker.h"
#include "../../common/logging/LoggingCategories.h"
#include "../window/GLTextureViewport.h"
#include "../decode/GpuDecodeTarget.h"
#include <QtGui/QOpenGLExtraFunctions>
#include <QtCore/QThread>
#include "../decode/TurboJpegDecoder.h"
#ifdef HAS_NVJPEG
#include "../decode/windows/NvJpegDecoder.h"
#endif
#ifdef HAS_VIDEOTOOLBOX
#include "../decode/macos/VideoToolboxDecoder.h"
#endif
#ifdef HAS_VAAPI
#include "../decode/linux/VaApiDecoder.h"
#endif
// ---- 构造/析构/基础方法 ----

DecodeWorker::DecodeWorker(QObject* parent)
    : QObject(parent) {
}

DecodeWorker::~DecodeWorker() {
    requestStop();
    // m_decoder 通过 unique_ptr 自动析构（释放 tjhandle 或 nvJPEG 句柄）
    // 仅当 cleanupGL() 未被预先调用（destroyDecodePipeline 的正常路径）时执行兜底清理。
    // 此时 DecodeThread 已停止，调用者确保已无跨线程事件风险。
    // m_glContext / m_glSurface 现在由 GpuDecodeTarget 管理生命周期。
    // cleanupGL() 已将其置空（正常路径）；此处仅防异常路径下的悬空指针。
    m_glContext = nullptr;
    m_glSurface = nullptr;
}

bool DecodeWorker::enqueueFrame(ScreenData screenData, const QSize& remoteSize) {
    if (!m_running.load()) {
        return false;
    }
    DecodeTask task;
    task.screenData = std::move(screenData);
    task.remoteSize = remoteSize;
    task.enqueueTs = std::chrono::steady_clock::now();  // 诊断：入队时刻
    const int dropped = m_queue.tryEnqueueDrainToLatest(std::move(task));
    if (dropped > 0) {
        qCDebug(lcClient) << "DecodeQueue drained:" << dropped << "old frames dropped";
    }
    return true;
}

void DecodeWorker::setFrameBuffer(TripleBuffer<FrameSlot>* buffer) {
    m_frameBuffer = buffer;
}

void DecodeWorker::start() {
    // 停止闩锁已置位（stop() 先于本排队事件派发）：不得复活工作循环
    if (m_stopRequested.load()) {
        qCInfo(lcClientSessionDecode) << "DecodeWorker::start() — stop already requested, skip";
        return;
    }
    m_running.store(true);

    // 优先级链: nvJPEG → VideoToolbox → VA-API → TurboJpeg (CPU)
#ifdef HAS_NVJPEG
    auto nv = std::make_unique<NvJpegDecoder>();
    if (nv->isAvailable()) {
        m_decoder = std::move(nv);
    }
#endif
#ifdef HAS_VIDEOTOOLBOX
    if (!m_decoder) {
        m_decoder = std::make_unique<VideoToolboxDecoder>();
    }
#endif
#ifdef HAS_VAAPI
    if (!m_decoder) {
        auto va = std::make_unique<VaApiDecoder>();
        if (va->isAvailable()) {
            m_decoder = std::move(va);
        }
    }
#endif
    if (!m_decoder) {
        m_decoder = std::make_unique<TurboJpegDecoder>();
    }

    qCInfo(lcClient) << "DecodeWorker: using decoder" << m_decoder->name();

    if (m_decodeTarget && !m_glInitFailed) {
        m_decoder->setDecodeTarget(m_decodeTarget);
    }

    workLoop();
}

void DecodeWorker::requestStop() {
    m_stopRequested.store(true);
    m_running.store(false);
}

void DecodeWorker::reportDecodeFailure(const QString& reason) {
    ++m_decodeFailStreak;
    // 失败流首帧上报到错误通路（UI 状态栏可见），后续帧仅限速日志——
    // 服务端持续发送坏帧时避免每帧一次信号/日志风暴（60 条/秒）
    if (m_decodeFailStreak == 1) {
        emit decodeError(RdError(ErrorCode::DecodeFailed, reason, "DecodeWorker"));
    }
    if (m_decodeFailStreak == 1 || m_decodeFailStreak % 30 == 0) {
        qCWarning(lcClientSessionDecode)
            << "DecodeWorker:" << reason << "— 连续失败" << m_decodeFailStreak << "帧";
    }
}

// ---- 工作循环 ----

void DecodeWorker::workLoop() {
    qCInfo(lcClient) << "DecodeWorker::workLoop() - Starting decode loop";

    while (m_running.load() && !m_stopRequested.load()
           && !QThread::currentThread()->isInterruptionRequested()) {
        if (!processOneFrame()) {
            // 队列为空时空闲退避，避免忙等吃满 CPU
            QThread::msleep(1);
        }
    }

    qCInfo(lcClient) << "DecodeWorker::workLoop() - Decode loop ended";
}

// ---- processOneFrame: JPEG 解码 + GL 上传 + TripleBuffer 写入 ----

bool DecodeWorker::processOneFrame() {
    DecodeTask task;
    if (!m_queue.tryDequeue(task)) {
        return false;  // 队列为空
    }

    using namespace std::chrono;
    const auto dequeueTs = steady_clock::now();

    // 诊断：队列等待时间（入队 → 出队）
    const auto queueWaitUs = duration_cast<microseconds>(
        dequeueTs - task.enqueueTs).count();

    const ScreenData& screenData = task.screenData;
    QSize remoteSize = task.remoteSize;

    // 1. JPEG 解码 + GL 上传（统一入口，无分支）
    const auto decodeStart = steady_clock::now();

    if (!m_decoder) {
        reportDecodeFailure(QStringLiteral("解码器未初始化"));
        return true;
    }

    int jpegWidth = 0, jpegHeight = 0;
    GLsync fence = nullptr;
    QImage fallbackImage;

    if (!m_decoder->decode(screenData.imageData,
                           &jpegWidth, &jpegHeight,
                           &fence, &fallbackImage)) {
        reportDecodeFailure(QStringLiteral("JPEG 解码失败"));
        return true;
    }

    m_decodeFailStreak = 0;  // 成功解码，重置失败流计数

    const auto decodeUs = duration_cast<microseconds>(
        steady_clock::now() - decodeStart).count();

    // 2. 更新 remoteSize（如果没有服务器缩放标志，用实际解码尺寸）
    if (!(screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED))
        || screenData.originalWidth == 0) {
        remoteSize = QSize(jpegWidth, jpegHeight);
    }

    // 3. 获取 TripleBuffer 写槽
    if (!m_frameBuffer) return true;

    FrameSlot* slot = nullptr;
    int idx = m_frameBuffer->acquireWrite(slot);
    if (!slot) return true;

    // 覆写前清理槽位中未被消费的旧 fence（latest-wins 丢帧场景），避免 GLsync 泄漏
    if (slot->uploadFence) {
        if (QOpenGLContext* ctx = QOpenGLContext::currentContext()) {
            ctx->extraFunctions()->glDeleteSync(slot->uploadFence);
        }
        slot->uploadFence = nullptr;
    }

    slot->remoteSize = remoteSize;
    slot->arrivalTs = steady_clock::now();

    // 4. GPU 上传结果（fence 已由解码器内部通过 target 生成）
    if (fence) {
        slot->uploadFence = fence;
    } else if (!fallbackImage.isNull()) {
        slot->image = fallbackImage;
    }

    // 5. 提交到 TripleBuffer
    m_frameBuffer->commitWrite(idx);

    // 6. 诊断：每 30 帧汇总输出两阶段耗时（成员变量：每实例独立，
    //    避免多会话 DecodeThread 并发读写共享 static 造成数据竞争）
    m_queueWaitAccumUs += queueWaitUs;
    m_queueWaitMaxUs = std::max(m_queueWaitMaxUs, queueWaitUs);
    m_decodeAccumUs += decodeUs;
    m_decodeMaxUs = std::max(m_decodeMaxUs, decodeUs);

    if (++m_diagFrameCount >= 30) {
        qCDebug(lcClientSessionDecode)
            << "[DecodeWorker 诊断] 近30帧耗时 (avg/max):"
            << "队列等待:" << (m_queueWaitAccumUs / 30 / 1000.0) << "/"
            << (m_queueWaitMaxUs / 1000.0) << "ms"
            << "JPEG解码:" << (m_decodeAccumUs / 30 / 1000.0) << "/"
            << (m_decodeMaxUs / 1000.0) << "ms";
        m_diagFrameCount = 0;
        m_queueWaitAccumUs = 0; m_queueWaitMaxUs = 0;
        m_decodeAccumUs = 0; m_decodeMaxUs = 0;
    }

    // 7. 请求 GUI 线程重绘
    if (m_glViewport) {
        m_glViewport->requestRepaint();
    }

    return true;  // 成功处理一帧
}

// ---- GL 方法 ----

bool DecodeWorker::initializeGL() {
    if (!m_decodeTarget || !m_decodeTarget->isReady()) {
        qCWarning(lcClient) << "DecodeWorker::initializeGL() — decode target not ready";
        return false;
    }

    // 在当前（解码）线程上延迟创建工作线程 GL 上下文
    // GpuDecodeTarget 的 ensureWorkerContext() 会在首次调用的线程上创建上下文，
    // 此处主动触发避免 Main 线程的 paintGL 回退路径抢先创建
    if (!m_decodeTarget->ensureWorkerContext()) {
        qCCritical(lcClient) << "DecodeWorker::initializeGL() — failed to create worker GL context";
        m_glInitFailed = true;
        return false;
    }

    m_glContext = m_decodeTarget->workerContext();
    m_glSurface = m_decodeTarget->offscreenSurface();

    // 激活上下文——后续所有 GL 操作依赖此调用
    if (!m_glContext->makeCurrent(m_glSurface)) {
        qCCritical(lcClient) << "DecodeWorker::initializeGL() — failed to make GL context current";

        // GL 上下文激活失败 → 显式标记，使后续 start() 跳过 GPU 路径退化为 CPU 解码
        m_glInitFailed = true;
        return false;
    }

    qCInfo(lcClient) << "DecodeWorker::initializeGL() — worker GL context ready on decode thread";
    return true;
}

void DecodeWorker::cleanupGL() {
    m_glViewport = nullptr;
    m_decoder.reset();

    // 在线程退出前清理 GpuDecodeTarget 的 GL 资源（PBO/纹理）
    // 此时 GL 上下文仍在当前线程，makeCurrent 安全
    if (m_decodeTarget) {
        m_decodeTarget->cleanup();
    }

    // 释放当前上下文（GpuDecodeTarget 持有所有权，我们只 release）
    if (m_glContext) {
        m_glContext->doneCurrent();
    }

    m_glContext = nullptr;
    m_glSurface = nullptr;
    m_decodeTarget = nullptr;
    qCInfo(lcClient) << "DecodeWorker::cleanupGL() — GL resources released";
}

void DecodeWorker::setGLViewport(GLTextureViewport* vp) {
    m_glViewport = vp;
}

void DecodeWorker::setDecodeTarget(GpuDecodeTarget* target) {
    m_decodeTarget = target;
}
