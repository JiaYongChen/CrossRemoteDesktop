#include "DecodeWorker.h"
#include "../../common/core/logging/LoggingCategories.h"
#ifndef QT_NO_OPENGL
#include "../window/GLTextureViewport.h"
#include "../decode/GpuDecodeTarget.h"
#include <QtGui/QOpenGLExtraFunctions>
#endif
#include <QtCore/QThread>
#include "../decode/TurboJpegDecoder.h"
#ifdef HAS_NVJPEG
#include "../decode/NvJpegDecoder.h"
#endif
// ---- 构造/析构/基础方法 ----

DecodeWorker::DecodeWorker(QObject* parent)
    : QObject(parent) {
}

DecodeWorker::~DecodeWorker() {
    requestStop();
    // m_decoder 通过 unique_ptr 自动析构（释放 tjhandle 或 nvJPEG 句柄）
#ifndef QT_NO_OPENGL
    // 仅当 cleanupGL() 未被预先调用（destroyDecodePipeline 的正常路径）时执行兜底清理。
    // 此时 DecodeThread 已停止，调用者确保已无跨线程事件风险。
    // m_glContext / m_glSurface 现在由 GpuDecodeTarget 管理生命周期。
    // cleanupGL() 已将其置空（正常路径）；此处仅防异常路径下的悬空指针。
    m_glContext = nullptr;
    m_glSurface = nullptr;
#endif
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
    m_running.store(true);

    // 优先级链: nvJPEG (NVIDIA CC 5.0+) → TurboJpeg (CPU)
#ifdef HAS_NVJPEG
    auto nv = std::make_unique<NvJpegDecoder>();
    if (nv->isAvailable()) {
        m_decoder = std::move(nv);
    }
#endif
    if (!m_decoder) {
        m_decoder = std::make_unique<TurboJpegDecoder>();
    }

    qCInfo(lcClient) << "DecodeWorker: using decoder" << m_decoder->name();

#ifndef QT_NO_OPENGL
    if (m_decodeTarget) {
        m_decoder->setDecodeTarget(m_decodeTarget);
    }
#endif

    workLoop();
}

void DecodeWorker::requestStop() {
    m_running.store(false);
}

// ---- 工作循环 ----

void DecodeWorker::workLoop() {
    qCInfo(lcClient) << "DecodeWorker::workLoop() - Starting decode loop";

    while (m_running.load()) {
        if (!processOneFrame()) {
            // 队列为空时空闲退避，避免忙等吃满 CPU
            QThread::msleep(1);
        }
    }

    qCInfo(lcClient) << "DecodeWorker::workLoop() - Decode loop ended";
    emit stopped();
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
        qCWarning(lcClient) << "DecodeWorker: no decoder available";
        emit decodeError(RdError(ErrorCode::DecodeFailed,
            QStringLiteral("解码器未初始化"), "DecodeWorker"));
        return true;
    }

    int jpegWidth = 0, jpegHeight = 0;
#ifndef QT_NO_OPENGL
    GLsync fence = nullptr;
#endif
    QImage fallbackImage;

#ifndef QT_NO_OPENGL
    if (!m_decoder->decode(screenData.imageData,
                           &jpegWidth, &jpegHeight,
                           &fence, &fallbackImage)) {
        emit decodeError(RdError(ErrorCode::DecodeFailed,
            QStringLiteral("JPEG 解码失败"), "DecodeWorker"));
        return true;
    }
#else
    if (!m_decoder->decode(screenData.imageData,
                           &jpegWidth, &jpegHeight,
                           &fallbackImage)) {
        emit decodeError(RdError(ErrorCode::DecodeFailed,
            QStringLiteral("JPEG 解码失败"), "DecodeWorker"));
        return true;
    }
#endif

    const auto decodeUs = duration_cast<microseconds>(
        steady_clock::now() - decodeStart).count();

    // 2. 更新 remoteSize（如果没有服务器缩放标志，用实际解码尺寸）
    if (!(screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED))
        || screenData.originalWidth == 0) {
        remoteSize = QSize(jpegWidth, jpegHeight);
    }

    // 3. 帧计数
    ++m_frameId;

    // 4. 获取 TripleBuffer 写槽
    if (!m_frameBuffer) return true;

    FrameSlot* slot = nullptr;
    int idx = m_frameBuffer->acquireWrite(slot);
    if (!slot) return true;

    slot->remoteSize = remoteSize;
    slot->arrivalTs = steady_clock::now();
    slot->frameId = static_cast<quint64>(m_frameId);

    qint64 glUploadUs = 0;

#ifndef QT_NO_OPENGL
    // 5. GPU 上传结果（fence 已由解码器内部通过 target 生成）
    if (fence) {
        slot->uploadFence = fence;
    } else if (!fallbackImage.isNull()) {
        slot->image = fallbackImage;
    }
#else
    slot->image = fallbackImage;
#endif

    // 6. 提交到 TripleBuffer
    m_frameBuffer->commitWrite(idx);

    // 7. 诊断：每 30 帧汇总输出三阶段耗时
    static int s_diagFrameCount = 0;
    static qint64 s_queueWaitAccumUs = 0, s_queueWaitMaxUs = 0;
    static qint64 s_decodeAccumUs = 0, s_decodeMaxUs = 0;
    static qint64 s_glUploadAccumUs = 0, s_glUploadMaxUs = 0;
    s_queueWaitAccumUs += queueWaitUs;
    s_queueWaitMaxUs = std::max(s_queueWaitMaxUs, queueWaitUs);
    s_decodeAccumUs += decodeUs;
    s_decodeMaxUs = std::max(s_decodeMaxUs, decodeUs);
    s_glUploadAccumUs += glUploadUs;
    s_glUploadMaxUs = std::max(s_glUploadMaxUs, glUploadUs);

    if (++s_diagFrameCount >= 30) {
        qCInfo(lcClientSessionDecode)
            << "[DecodeWorker 诊断] 近30帧耗时 (avg/max):"
            << "队列等待:" << (s_queueWaitAccumUs / 30 / 1000.0) << "/"
            << (s_queueWaitMaxUs / 1000.0) << "ms"
            << "JPEG解码:" << (s_decodeAccumUs / 30 / 1000.0) << "/"
            << (s_decodeMaxUs / 1000.0) << "ms"
            << "GL上传:" << (s_glUploadAccumUs / 30 / 1000.0) << "/"
            << (s_glUploadMaxUs / 1000.0) << "ms";
        s_diagFrameCount = 0;
        s_queueWaitAccumUs = 0; s_queueWaitMaxUs = 0;
        s_decodeAccumUs = 0; s_decodeMaxUs = 0;
        s_glUploadAccumUs = 0; s_glUploadMaxUs = 0;
    }

#ifndef QT_NO_OPENGL
    // 8. 请求 GUI 线程重绘
    if (m_glViewport) {
        m_glViewport->requestRepaint();
    }
#endif

    return true;  // 成功处理一帧
}

// ---- GL 方法 ----

#ifndef QT_NO_OPENGL

bool DecodeWorker::initializeGL(QOpenGLContext* shareContext) {
    Q_UNUSED(shareContext);
    if (!m_decodeTarget || !m_decodeTarget->isReady()) {
        qCWarning(lcClient) << "DecodeWorker::initializeGL() — decode target not ready";
        return false;
    }

    // 在当前（解码）线程上延迟创建工作线程 GL 上下文
    // GpuDecodeTarget 的 ensureWorkerContext() 会在首次调用的线程上创建上下文，
    // 此处主动触发避免 Main 线程的 paintGL 回退路径抢先创建
    if (!m_decodeTarget->ensureWorkerContext()) {
        qCWarning(lcClient) << "DecodeWorker::initializeGL() — failed to create worker GL context";
        return false;
    }

    m_glContext = m_decodeTarget->workerContext();
    m_glSurface = m_decodeTarget->offscreenSurface();

    // 激活上下文——后续所有 GL 操作依赖此调用
    if (!m_glContext->makeCurrent(m_glSurface)) {
        qCWarning(lcClient) << "DecodeWorker::initializeGL() — failed to make GL context current";
        m_glUploadReady = false;
        return false;
    }

    m_glUploadReady = true;
    qCInfo(lcClient) << "DecodeWorker::initializeGL() — worker GL context ready on decode thread";
    return true;
}

void DecodeWorker::moveGLToThread(QThread* target) {
    if (m_glContext) m_glContext->moveToThread(target);
    if (m_glSurface) m_glSurface->moveToThread(target);
    m_glUploadReady = false;
}

void DecodeWorker::cleanupGL() {
    m_glUploadReady = false;
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

#endif
