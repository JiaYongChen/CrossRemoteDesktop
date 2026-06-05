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
    return m_queue.tryEnqueue(std::move(task));  // 非阻塞：满时丢弃新帧
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

#ifndef QT_NO_OPENGL
    // 线程退出前在自身上下文中清理 GL 资源，确保线程安全
    cleanupGL();
#endif

    qCInfo(lcClient) << "DecodeWorker::workLoop() - Decode loop ended";
    emit stopped();
}

// ---- processOneFrame: JPEG 解码 + GL 上传 + TripleBuffer 写入 ----

void DecodeWorker::processOneFrame() {
    // === 首帧豁免：直接解码提交，打破启动死锁 ===
    // paintGL 需要 TripleBuffer 有帧才会正常工作；
    // 预解码管线需要 paintGL 发信号才能提交帧。
    // 首帧绕过整个信号机制，直接做一次完整的解码→提交，启动 pipeline。
    if (m_firstFrame) {
        m_firstFrame = false;
        DecodeTask task;
        if (!m_queue.dequeue(task)) {
            return;  // 队列无数据，等待下次循环重试
        }

        const ScreenData& screenData = task.screenData;
        QSize remoteSize = task.remoteSize;

        // JPEG 解码
        QBuffer buffer(const_cast<QByteArray*>(&screenData.imageData));
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "JPEG");
        reader.setAutoTransform(true);
        if (!reader.read(&m_decodeBuffer) || m_decodeBuffer.isNull()) {
            qCWarning(lcClient) << "DecodeWorker: first frame JPEG decode failed";
            emit decodeError(QStringLiteral("JPEG 解码失败"));
            m_firstFrame = true;  // 恢复标记，下次重试
            return;
        }

        if (!(screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED))
            || screenData.originalWidth == 0) {
            remoteSize = m_decodeBuffer.size();
        }
        ++m_frameId;

        // 直接提交到 TripleBuffer（不走预解码槽）
        if (m_frameBuffer) {
            FrameSlot* slot = nullptr;
            int idx = m_frameBuffer->acquireWrite(slot);
            if (slot) {
                slot->remoteSize = remoteSize;
                slot->frameId   = static_cast<quint64>(m_frameId);
                slot->arrivalTs = std::chrono::steady_clock::now();
#ifndef QT_NO_OPENGL
                if (m_glUploadReady && m_glContext && m_glSurface && m_glViewport) {
                    m_glContext->makeCurrent(m_glSurface);
                    GLsync fence = m_glViewport->uploadFromWorker(m_decodeBuffer);
                    m_glContext->doneCurrent();
                    if (fence) {
                        slot->uploadFence = fence;
                    } else {
                        slot->image = m_decodeBuffer;
                    }
                } else {
                    slot->image = m_decodeBuffer;
                }
#else
                slot->image = m_decodeBuffer;
#endif
                m_frameBuffer->commitWrite(idx);
#ifndef QT_NO_OPENGL
                if (m_glViewport) {
                    m_glViewport->requestRepaint();
                }
#endif
            }
        }

        // 首帧提交后直接返回，不等待 renderingDone。
        // paintGL 由 VSync（60Hz 约 16ms）自然驱动，触发 startPredecode → 启动正常循环。
        qCInfo(lcClient) << "DecodeWorker: first frame committed, entering normal predecode loop";
        return;
    }

    // === 阶段 A：等待 paintGL 开始渲染（速率同步） ===
    while (m_running.load() && !m_startPredecode.load(std::memory_order_acquire)) {
        QThread::msleep(1);
    }
    if (!m_running.load()) return;
    m_startPredecode.store(false, std::memory_order_release);

    // === 阶段 B：预解码下一帧（paintGL 渲染期间，并行工作） ===
    m_predecodeSlot.clear();
    {
        DecodeTask task;
        if (!m_queue.dequeue(task)) {
            goto phaseC;  // 队列已停止或无数据，跳过本轮
        }

        const ScreenData& screenData = task.screenData;
        QSize remoteSize = task.remoteSize;

        // JPEG 解码
        QBuffer buffer(const_cast<QByteArray*>(&screenData.imageData));
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "JPEG");
        reader.setAutoTransform(true);
        if (!reader.read(&m_decodeBuffer) || m_decodeBuffer.isNull()) {
            qCWarning(lcClient) << "DecodeWorker: JPEG decode failed, size:"
                                << screenData.imageData.size();
            emit decodeError(QStringLiteral("JPEG 解码失败"));
            goto phaseC;
        }

        if (!(screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED))
            || screenData.originalWidth == 0) {
            remoteSize = m_decodeBuffer.size();
        }
        ++m_frameId;

#ifndef QT_NO_OPENGL
        // GL 上传（写入非显示纹理，GPU DMA 与 paintGL 并行）
        if (m_glUploadReady && m_glContext && m_glSurface && m_glViewport) {
            m_glContext->makeCurrent(m_glSurface);
            GLsync fence = m_glViewport->uploadFromWorker(m_decodeBuffer);
            m_glContext->doneCurrent();
            if (fence) {
                m_predecodeSlot.fence = fence;
                m_predecodeSlot.remoteSize = remoteSize;
                m_predecodeSlot.frameId = static_cast<quint64>(m_frameId);
                m_predecodeSlot.valid = true;
            }
        } else {
            // GL 未就绪，回退到 image 路径
            m_predecodeSlot.image = m_decodeBuffer;
            m_predecodeSlot.remoteSize = remoteSize;
            m_predecodeSlot.frameId = static_cast<quint64>(m_frameId);
            m_predecodeSlot.valid = true;
        }
#else
        // 无 GL 回退：image 数据直接存入预解码槽
        m_predecodeSlot.image = m_decodeBuffer;
        m_predecodeSlot.remoteSize = remoteSize;
        m_predecodeSlot.frameId = static_cast<quint64>(m_frameId);
        m_predecodeSlot.valid = true;
#endif
    }

    m_predecodedReady.store(true, std::memory_order_release);

phaseC:
    // === 阶段 C：等待 paintGL 渲染完成 ===
    while (m_running.load() && !m_renderingDone.load(std::memory_order_acquire)) {
        QThread::msleep(1);
    }
    if (!m_running.load()) return;
    m_renderingDone.store(false, std::memory_order_release);

    // === 阶段 D：提交预解码帧（零延迟，仅 commitWrite） ===
    if (m_predecodedReady.load(std::memory_order_acquire) && m_predecodeSlot.valid && m_frameBuffer) {
        FrameSlot* slot = nullptr;
        int idx = m_frameBuffer->acquireWrite(slot);
        if (slot) {
            slot->remoteSize = m_predecodeSlot.remoteSize;
            slot->frameId   = m_predecodeSlot.frameId;
            slot->arrivalTs = std::chrono::steady_clock::now();
#ifndef QT_NO_OPENGL
            if (m_predecodeSlot.fence) {
                slot->uploadFence = m_predecodeSlot.fence;
                m_predecodeSlot.fence = nullptr;  // 所有权转移
            } else {
                slot->image = m_predecodeSlot.image;
            }
#else
            slot->image = m_predecodeSlot.image;
#endif
            m_frameBuffer->commitWrite(idx);
#ifndef QT_NO_OPENGL
            if (m_glViewport) {
                m_glViewport->requestRepaint();
            }
#endif
        }
    }
    m_predecodedReady.store(false, std::memory_order_release);
    // 循环回到阶段 A，等待下一次 paintGL 触发
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
