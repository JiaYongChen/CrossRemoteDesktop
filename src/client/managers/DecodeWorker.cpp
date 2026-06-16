#include "DecodeWorker.h"
#include "../../common/core/logging/LoggingCategories.h"
#ifndef QT_NO_OPENGL
#include "../window/GLTextureViewport.h"
#include <QtGui/QOpenGLExtraFunctions>
#endif
#include <QtCore/QThread>

// ---- 构造/析构/基础方法 ----

DecodeWorker::DecodeWorker(QObject* parent)
    : QObject(parent) {
}

DecodeWorker::~DecodeWorker() {
    requestStop();
    if (m_tjDecompress) {
        tjDestroy(m_tjDecompress);
        m_tjDecompress = nullptr;
    }
#ifndef QT_NO_OPENGL
    // 仅当 cleanupGL() 未被预先调用（destroyDecodePipeline 的正常路径）时执行兜底清理。
    // 此时 DecodeThread 已停止，调用者确保已无跨线程事件风险。
    delete m_glContext;
    m_glContext = nullptr;
    delete m_glSurface;
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
    return m_queue.tryEnqueue(std::move(task));
}

void DecodeWorker::setFrameBuffer(TripleBuffer<FrameSlot>* buffer) {
    m_frameBuffer = buffer;
}

void DecodeWorker::start() {
    m_running.store(true);
    // 直接调用 workLoop() 替代 QTimer::singleShot(0,...)。
    // workLoop() 本身就是阻塞式 while 循环，无需额外事件循环延迟。
    // start() 通过 QueuedConnection 调用，已在正确的线程上下文中。
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

    // 1. turbojpeg 解码（计时）
    const auto decodeStart = steady_clock::now();

    // 懒初始化 turbojpeg 解压句柄（线程局部，复用）
    if (!m_tjDecompress) {
        m_tjDecompress = tjInitDecompress();
        if (!m_tjDecompress) {
            qCWarning(lcClient) << "DecodeWorker: tjInitDecompress failed";
            emit decodeError(RdError(ErrorCode::DecodeFailed, QStringLiteral("turbojpeg 初始化失败"), "DecodeWorker"));
            return true;
        }
    }

    // 读取 JPEG 头部获取图像尺寸
    int jpegWidth = 0, jpegHeight = 0, jpegSubsamp = 0, jpegColorspace = 0;
    if (tjDecompressHeader3(m_tjDecompress,
            reinterpret_cast<const unsigned char*>(screenData.imageData.constData()),
            static_cast<unsigned long>(screenData.imageData.size()),
            &jpegWidth, &jpegHeight, &jpegSubsamp, &jpegColorspace) != 0) {
        qCWarning(lcClient) << "DecodeWorker: tjDecompressHeader3 failed:"
                            << tjGetErrorStr2(m_tjDecompress)
                            << "size:" << screenData.imageData.size();
        emit decodeError(RdError(ErrorCode::DecodeFailed, QStringLiteral("JPEG 头部解析失败"), "DecodeWorker"));
        return true;
    }

    // 复用解码缓冲区，仅在尺寸不匹配时重建
    if (m_decodeBuffer.isNull()
        || m_decodeBuffer.width() != jpegWidth
        || m_decodeBuffer.height() != jpegHeight) {
        m_decodeBuffer = QImage(jpegWidth, jpegHeight, QImage::Format_RGB888);
    }

    // turbojpeg 解压到 QImage 像素缓冲区（零拷贝）
    if (tjDecompress2(m_tjDecompress,
            reinterpret_cast<const unsigned char*>(screenData.imageData.constData()),
            static_cast<unsigned long>(screenData.imageData.size()),
            m_decodeBuffer.bits(),
            jpegWidth, 0, jpegHeight, TJPF_RGB, TJFLAG_FASTDCT) != 0) {
        qCWarning(lcClient) << "DecodeWorker: tjDecompress2 failed:"
                            << tjGetErrorStr2(m_tjDecompress)
                            << "size:" << screenData.imageData.size()
                            << "dimensions:" << jpegWidth << "x" << jpegHeight;
        emit decodeError(RdError(ErrorCode::DecodeFailed, QStringLiteral("JPEG 解码失败"), "DecodeWorker"));
        return true;
    }

    const auto decodeUs = duration_cast<microseconds>(
        steady_clock::now() - decodeStart).count();

    QImage& image = m_decodeBuffer;

    // 2. 更新 remoteSize（如果没有服务器缩放标志，用实际解码尺寸）
    if (!(screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED))
        || screenData.originalWidth == 0) {
        remoteSize = image.size();
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
    // 5. GL 纹理上传（计时）
    static int s_glUploadDiagCount = 0;
    if (m_glUploadReady && m_glContext && m_glSurface && m_glViewport) {
        // GPU 生产者背压：paintGL 连续跳过 >=3 帧时隔帧跳过
        static int s_backoffCounter = 0;
        const bool gpuOverloaded = m_glViewport->consecutiveSkips() >= 3;
        const bool skipThisUpload = gpuOverloaded && (++s_backoffCounter % 2 == 0);
        if (gpuOverloaded && s_backoffCounter <= 3) {
            qCDebug(lcClient) << "GPU backpressure: skipping GL upload, skips="
                              << m_glViewport->consecutiveSkips();
        }

        if (!skipThisUpload) {
            ++s_glUploadDiagCount;
            if (s_glUploadDiagCount <= 3 || s_glUploadDiagCount % 100 == 0) {
                qCDebug(lcClient) << "DecodeWorker: GL upload #" << s_glUploadDiagCount
                                  << "frame" << m_frameId;
            }
            m_glContext->makeCurrent(m_glSurface);
            // 删除上一帧的 fence
            if (slot->uploadFence) {
                auto* f = m_glContext->extraFunctions();
                if (f) f->glDeleteSync(slot->uploadFence);
                slot->uploadFence = nullptr;
            }
            const auto glStart = steady_clock::now();
            GLsync fence = m_glViewport->uploadFromWorker(image);
            glUploadUs = duration_cast<microseconds>(
                steady_clock::now() - glStart).count();
            if (fence) {
                slot->uploadFence = fence;
            }
            m_glContext->doneCurrent();
        } else {
            // 背压：跳过 GL 上传，保留 image 供 GUI 线程回退
            slot->image = image;
        }
    } else {
        static int s_glSkipDiagCount = 0;
        if (++s_glSkipDiagCount <= 3) {
            qCWarning(lcClient) << "DecodeWorker: GL upload skipped -"
                                << "ready:" << m_glUploadReady
                                << "ctx:" << (m_glContext != nullptr)
                                << "surface:" << (m_glSurface != nullptr)
                                << "viewport:" << (m_glViewport != nullptr);
        }
        slot->image = image;
    }
#else
    slot->image = image;
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
        qCInfo(lcRefreshMetrics)
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
    if (!shareContext) {
        qCWarning(lcClient) << "DecodeWorker::initializeGL() - No share context provided";
        return false;
    }

    m_glContext = new QOpenGLContext();
    m_glContext->setShareContext(shareContext);
    m_glContext->setFormat(shareContext->format());
    if (!m_glContext->create()) {
        qCWarning(lcClient) << "DecodeWorker::initializeGL() - Failed to create shared GL context";
        delete m_glContext;
        m_glContext = nullptr;
        return false;
    }

    m_glSurface = new QOffscreenSurface();
    m_glSurface->setFormat(m_glContext->format());
    m_glSurface->create();

    // 不再需要 moveToThread：此方法现在通过 QueuedConnection 在 DecodeThread 中执行，
    // QOffscreenSurface 内部的 QWindow 原生资源直接创建在正确的线程中。
    // 在非 GUI 线程创建 QOffscreenSurface 会产生 Qt 警告但功能正常。

    m_glUploadReady = true;
    qCInfo(lcClient) << "DecodeWorker::initializeGL() - GL context ready on"
                      << (this->thread() ? this->thread()->objectName() : "null");
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
    if (m_tjDecompress) {
        tjDestroy(m_tjDecompress);
        m_tjDecompress = nullptr;
    }
    delete m_glContext;
    m_glContext = nullptr;
    delete m_glSurface;
    m_glSurface = nullptr;
    qCInfo(lcClient) << "DecodeWorker::cleanupGL() - GL resources deleted on DecodeThread";
}

void DecodeWorker::setGLViewport(GLTextureViewport* vp) {
    m_glViewport = vp;
}

#endif
