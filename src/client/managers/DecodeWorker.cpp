#include "DecodeWorker.h"
#include "../../common/core/logging/LoggingCategories.h"
#ifndef QT_NO_OPENGL
#include "../window/GLTextureViewport.h"
#include <QtGui/QOpenGLExtraFunctions>
#endif
#include <QtCore/QBuffer>
#include <QtGui/QImageReader>
#include <QtCore/QThread>

// ---- 构造/析构/基础方法 ----

DecodeWorker::DecodeWorker(QObject* parent)
    : QObject(parent) {
}

DecodeWorker::~DecodeWorker() {
    requestStop();
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
    return m_queue.enqueue(std::move(task));
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
    m_queue.stop();
}

// ---- 工作循环 ----

void DecodeWorker::workLoop() {
    qCInfo(lcClient) << "DecodeWorker::workLoop() - Starting decode loop";

    while (m_running.load()) {
        processOneFrame();
    }

    qCInfo(lcClient) << "DecodeWorker::workLoop() - Decode loop ended";
    emit stopped();
}

// ---- processOneFrame: JPEG 解码 + GL 上传 + TripleBuffer 写入 ----

void DecodeWorker::processOneFrame() {
    DecodeTask task;
    if (!m_queue.dequeue(task)) {
        return;  // 队列已停止
    }

    const ScreenData& screenData = task.screenData;
    QSize remoteSize = task.remoteSize;

    // 1. JPEG 解码
    QBuffer buffer(const_cast<QByteArray*>(&screenData.imageData));
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "JPEG");
    reader.setAutoTransform(true);
    const bool loaded = reader.read(&m_decodeBuffer);

    if (!loaded || m_decodeBuffer.isNull()) {
        qCWarning(lcClient) << "DecodeWorker::processOneFrame() - JPEG decode failed, size:"
                            << screenData.imageData.size();
        emit decodeError(QStringLiteral("JPEG 解码失败"));
        return;
    }

    QImage& image = m_decodeBuffer;

    // 2. 更新 remoteSize（如果没有服务器缩放标志，用实际解码尺寸）
    if (!(screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED))
        || screenData.originalWidth == 0) {
        remoteSize = image.size();
    }

    // 3. 帧计数
    ++m_frameId;

    // 4. 获取 TripleBuffer 写槽
    if (!m_frameBuffer) return;

    FrameSlot* slot = nullptr;
    int idx = m_frameBuffer->acquireWrite(slot);
    if (!slot) return;

    slot->remoteSize = remoteSize;
    slot->arrivalTs = std::chrono::steady_clock::now();
    slot->frameId = static_cast<quint64>(m_frameId);

#ifndef QT_NO_OPENGL
    // 5. GL 纹理上传（从 SessionManager 迁移的逻辑）
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
            GLsync fence = m_glViewport->uploadFromWorker(image);
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

#ifndef QT_NO_OPENGL
    // 7. 请求 GUI 线程重绘
    if (m_glViewport) {
        m_glViewport->requestRepaint();
    }
#endif
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
