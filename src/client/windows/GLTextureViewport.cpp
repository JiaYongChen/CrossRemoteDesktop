#ifndef QT_NO_OPENGL

#include "GLTextureViewport.h"
#include "../decode/GpuDecodeTarget.h"
#include "../../common/config/GuiConstants.h"
#include "../../common/logging/LoggingCategories.h"
#include "CursorManager.h"

#include <QtGui/QOpenGLExtraFunctions>
#include <QtGui/QOpenGLContext>
#include <QtGui/QPainter>
#include <algorithm>  // std::max
#include <chrono>

// Vertex shader: pass through position and texture coordinates
static const char* s_vertexShaderSource = R"(
    #version 330 core
    layout(location = 0) in vec2 aPosition;
    layout(location = 1) in vec2 aTexCoord;
    out vec2 vTexCoord;
    void main() {
        gl_Position = vec4(aPosition, 0.0, 1.0);
        vTexCoord = aTexCoord;
    }
)";

// Fragment shader: sample texture
static const char* s_fragmentShaderSource = R"(
    #version 330 core
    in vec2 vTexCoord;
    out vec4 fragColor;
    uniform sampler2D uTexture;
    void main() {
        fragColor = texture(uTexture, vTexCoord);
    }
)";

GLTextureViewport::GLTextureViewport(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_vertexBuffer(QOpenGLBuffer::VertexBuffer) {
    // Request OpenGL 3.3 Core profile
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapInterval(1);
    setFormat(format);
}

GLTextureViewport::~GLTextureViewport() {
    // 在 vtable 降级前断开 aboutToBeDestroyed 信号，避免基类析构时 Qt 断言崩溃
    if (QOpenGLContext* ctx = context()) {
        disconnect(ctx, &QOpenGLContext::aboutToBeDestroyed,
                   this, &GLTextureViewport::cleanupGL);
    }

    // TripleBuffer（归属 DecodePipeline）在关窗路径中可能先于本视口析构
    // （session deleteLater 先入队，窗口 WA_DeleteOnClose 后入队）。
    // 清空 pending-fence 引用，避免后续 cleanupGL 解引用已释放内存（UAF）。
    m_pendingFenceSlot = nullptr;
    m_pendingFenceIdx = -1;

    // 仅在上下文有效时清理 GL 资源
    if (QOpenGLContext* ctx = context(); ctx && ctx->isValid()) {
        makeCurrent();
        cleanupGL();
        doneCurrent();
    }
}

void GLTextureViewport::initializeGL() {
    initializeOpenGLFunctions();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    if ( !initializeShaders() ) {
        qCCritical(lcClientGL) << "Failed to initialize shaders";
        return;
    }

    initializeGeometry();

    // 创建 GpuDecodeTarget
    m_decodeTarget = new GpuDecodeTarget(context());
    if (!m_decodeTarget->initialize()) {
        qCCritical(lcClientGL) << "Failed to initialize GpuDecodeTarget";
        delete m_decodeTarget;
        m_decodeTarget = nullptr;
    }

    // CursorManager 的 GL 资源在首次渲染时懒初始化

    m_glInitialized = true;

    // Prepare for context loss recovery
    connect(context(), &QOpenGLContext::aboutToBeDestroyed,
            this, &GLTextureViewport::cleanupGL);

    qCInfo(lcClientGL) << "OpenGL initialized:"
        << "vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR))
        << "renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
        << "version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));

    // 通知监听者（RemoteDesktopSession → DecodePipeline）GL 已就绪；
    // worker 解码上下文由 GpuDecodeTarget 在解码线程内自建，不与此上下文共享
    emit glContextReady(context());
}

bool GLTextureViewport::initializeShaders() {
    m_shaderProgram = new QOpenGLShaderProgram(this);

    if ( !m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertexShaderSource) ) {
        qCCritical(lcClientGL) << "Vertex shader compilation failed:"
            << m_shaderProgram->log();
        return false;
    }

    if ( !m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragmentShaderSource) ) {
        qCCritical(lcClientGL) << "Fragment shader compilation failed:"
            << m_shaderProgram->log();
        return false;
    }

    if ( !m_shaderProgram->link() ) {
        qCCritical(lcClientGL) << "Shader program link failed:"
            << m_shaderProgram->log();
        return false;
    }

    return true;
}

void GLTextureViewport::initializeGeometry() {
    // Fullscreen quad: 2 triangles covering NDC [-1, 1]
    // Each vertex: position (x,y) + texcoord (u,v)
    // Note: texcoord Y is flipped (1->0) because QImage origin is top-left
    // while OpenGL origin is bottom-left
    static const float vertices[] = {
        // Position    TexCoord
        -1.0f, -1.0f,  0.0f, 1.0f,  // bottom-left
         1.0f, -1.0f,  1.0f, 1.0f,  // bottom-right
        -1.0f,  1.0f,  0.0f, 0.0f,  // top-left
         1.0f,  1.0f,  1.0f, 0.0f,  // top-right
    };

    m_vao.create();
    QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

    m_vertexBuffer.create();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(vertices, sizeof(vertices));

    // Position attribute (location = 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          nullptr);

    // TexCoord attribute (location = 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    m_vertexBuffer.release();
}

void GLTextureViewport::cleanupGL() {
    // 通知外部持有者：GL 资源（包括 GpuDecodeTarget）即将被销毁——
    // RemoteDesktopSession 应在此信号后停止解码管线，避免 DecodeWorker
    // 持有的裸指针在 processOneFrame 中悬空（use-after-free）
    emit glResourcesAboutToBeDestroyed();

    // aboutToBeDestroyed 路径不保证上下文为 current：析构路径已 makeCurrent，
    // 此处加防御性 makeCurrent 避免无 current 下 glDeleteSync/glDeleteTextures UB。
    // QOpenGLContext::surface() 返回 QOpenGLWidget 的 QWindow（始终非空），
    // API 要求 makeCurrent(curSurface) 而非 surrogates。
    if (QOpenGLContext* ctx = context(); ctx && ctx->surface()) {
        ctx->makeCurrent(ctx->surface());
    }

    // 丢弃未决的超时重试 fence（上下文即将销毁）
    if (m_pendingFenceSlot && m_pendingFenceSlot->uploadFence) {
        if (QOpenGLContext* ctx = context()) {
            ctx->extraFunctions()->glDeleteSync(m_pendingFenceSlot->uploadFence);
        }
        m_pendingFenceSlot->uploadFence = nullptr;
    }
    m_pendingFenceSlot = nullptr;
    m_pendingFenceIdx = -1;

    if (m_decodeTarget) {
        m_decodeTarget->cleanup();
        delete m_decodeTarget;
        m_decodeTarget = nullptr;
    }
    m_vertexBuffer.destroy();
    m_vao.destroy();
    delete m_shaderProgram;
    m_shaderProgram = nullptr;
    m_glInitialized = false;
    m_textureSize = QSize();  // reset

    // 清理光标 GL 资源
    if (m_cursorTex != 0) {
        glDeleteTextures(1, &m_cursorTex);
        m_cursorTex = 0;
    }
    m_cursorVAO.destroy();
    m_cursorVBO.destroy();
    m_cursorGLInit = false;

    // 清理回退纹理
    if (m_fallbackTexture != 0) {
        glDeleteTextures(1, &m_fallbackTexture);
        m_fallbackTexture = 0;
    }
    m_fallbackTexSize = QSize();
}

void GLTextureViewport::ensureFallbackTexture(int width, int height) {
    if (m_fallbackTexture != 0 && m_fallbackTexSize == QSize(width, height))
        return;

    if (m_fallbackTexture != 0) {
        glDeleteTextures(1, &m_fallbackTexture);
        m_fallbackTexture = 0;
    }

    glGenTextures(1, &m_fallbackTexture);
    glBindTexture(GL_TEXTURE_2D, m_fallbackTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    m_fallbackTexSize = QSize(width, height);
}

void GLTextureViewport::uploadFallbackTexture(const QImage& image) {
    ensureFallbackTexture(image.width(), image.height());
    // QImage 可能是 RGB888 或 ARGB32，需要转换
    QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    glBindTexture(GL_TEXTURE_2D, m_fallbackTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rgb.width(), rgb.height(),
                    GL_RGB, GL_UNSIGNED_BYTE, rgb.constBits());
}

void GLTextureViewport::forceRepaint() {
    m_textureDirty = true;
    update();
}

bool GLTextureViewport::requestRepaint() {
    // 仅当前一帧已被 paintGL 消费后才排队新的 paint 事件，
    // 避免在 GUI 线程事件队列中堆积冗余事件。
    // 此方法可被任意线程安全调用。
    bool expected = false;
    if ( m_needsRepaint.compare_exchange_strong(expected, true) ) {
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
        return true;
    }
    return false;
}

void GLTextureViewport::CheckForNewFrameAfterPaint(int consumedSlot) {
    // 仅在本次 paintGL 确实消费了一帧之后才检查。
    // 若 peekReady() 返回不同于 consumedSlot 的索引，说明在 paintGL 执行
    // 期间解码线程又提交了新帧。此时立即 CAS 排队 update()，
    // 将帧间等待时间从"下一个 VSync"缩短到"下一个事件循环迭代"。
    if (!m_frameBuffer || consumedSlot < 0) {
        return;
    }

    const int readySlot = m_frameBuffer->peekReady();
    if (readySlot >= 0 && readySlot != consumedSlot) {
        bool expected = false;
        if (m_needsRepaint.compare_exchange_strong(expected, true)) {
            QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
        }
    }
}

void GLTextureViewport::resizeGL(int w, int h) {
    Q_UNUSED(w)
    Q_UNUSED(h)
    updateRenderRect();
}

void GLTextureViewport::paintGL() {
    if (!m_shaderProgram) {
        // 早退也必须复位重绘门闩，否则 requestRepaint 的 CAS 永久失败（饿死）
        m_needsRepaint.store(false, std::memory_order_release);
        return;
    }

    // GpuDecodeTarget 未就绪或初始化失败，跳过绘制
    if (!m_decodeTarget) {
        m_needsRepaint.store(false, std::memory_order_release);
        return;
    }

    // Check triple buffer for new frames (lock-free, atomic read)
    static int s_paintCount = 0;
    if ( ++s_paintCount <= 3 )
        qCDebug(lcClientGL) << "paintGL called, frameBuffer:" << (m_frameBuffer != nullptr);
    m_consumedSlot = -1;  // 每次 paintGL 重置
    if ( m_frameBuffer ) {
        FrameSlot* slot = nullptr;
        int idx = -1;

        // fence 超时重试：若上次留有未就绪 fence 且期间无新帧提交，重查同一槽位；
        // 有新帧则丢弃过期 fence（latest-wins），改为消费新帧。
        if ( m_pendingFenceSlot ) {
            if ( m_frameBuffer->peekReady() == m_pendingFenceIdx ) {
                slot = m_pendingFenceSlot;
                idx = m_pendingFenceIdx;
            } else if ( m_pendingFenceSlot->uploadFence ) {
                context()->extraFunctions()->glDeleteSync(m_pendingFenceSlot->uploadFence);
                m_pendingFenceSlot->uploadFence = nullptr;
            }
            m_pendingFenceSlot = nullptr;
            m_pendingFenceIdx = -1;
        }

        if ( !slot ) {
            idx = m_frameBuffer->getReadSlot(slot);
        }
        // GL 上传路径中 image 可能为空（像素已通过 uploadFromWorker 写入纹理），
        // uploadFence 作为备选有效性指示器。
        if ( idx >= 0 && slot && (!slot->image.isNull() || slot->uploadFence) ) {
            m_consumedSlot = idx;  // 记录已消费的槽位，供帧尾检查新帧用
            if ( slot->uploadFence ) {
                // Worker thread already uploaded this frame to the shared
                // texture via its shared GL context. Check if the GPU has
                // finished the DMA transfer.
                auto* f = context()->extraFunctions();
                Q_ASSERT(f);
                // 诊断：测量 fence 等待耗时
                const auto fenceStart = std::chrono::steady_clock::now();
                // timeout=16ms: 冷 GPU 首次 DMA 传输可能超过 5ms（GTX 650 等老旧显卡）。
                // 过短的超时导致连续 fence 超时 → 强制跳过 → 渲染未就绪纹理（黑屏）。
                // 16ms = 一帧时长（60fps），给 GPU 充足完成窗口且不引入可见延迟。
                const GLenum result = f->glClientWaitSync(
                    slot->uploadFence, GL_SYNC_FLUSH_COMMANDS_BIT, 16000000 /* 16ms */);
                const auto fenceUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - fenceStart).count();
                static int s_fenceDiagCount = 0;
                ++s_fenceDiagCount;
                if ( s_fenceDiagCount <= 3 || s_fenceDiagCount % 30 == 0 )
                    qCDebug(lcClientGL) << "paintGL fence #" << s_fenceDiagCount << "result:" << result
                        << (result == GL_ALREADY_SIGNALED ? "SIGNALED" :
                            result == GL_CONDITION_SATISFIED ? "SATISFIED" :
                            result == GL_TIMEOUT_EXPIRED ? "TIMEOUT" : "OTHER")
                        << "wait:" << (fenceUs / 1000.0) << "ms";

                if ( result == GL_ALREADY_SIGNALED ||
                     result == GL_CONDITION_SATISFIED ) {
                    m_consecutiveFenceTimeouts = 0;
                    // GPU upload complete — texture is ready to draw.
                    f->glDeleteSync(slot->uploadFence);
                    slot->uploadFence = nullptr;
                    m_decodeTarget->swapDisplay();
                    m_textureDirty = true;
                    // 同步纹理尺寸
                    if (m_decodeTarget->textureWidth() > 0 &&
                        QSize(m_decodeTarget->textureWidth(), m_decodeTarget->textureHeight()) != m_textureSize) {
                        m_textureSize = QSize(m_decodeTarget->textureWidth(), m_decodeTarget->textureHeight());
                        if (m_renderRect.isEmpty()) updateRenderRect();
                    }
                } else {
                    // GL_TIMEOUT_EXPIRED or GL_WAIT_FAILED: GPU 未完成 DMA。
                    ++m_consecutiveFenceTimeouts;
                    if ( m_consecutiveFenceTimeouts >= 5 ) {
                        // 连续 5 次超时（~80ms）：强制放弃 fence，避免画面卡死。
                        // 可能显示部分上传的纹理，但远好于冻结。
                        qCWarning(lcClientGL) << "paintGL: force-skipping stuck fence after"
                                               << m_consecutiveFenceTimeouts << "timeouts";
                        f->glDeleteSync(slot->uploadFence);
                        slot->uploadFence = nullptr;
                        m_decodeTarget->swapDisplay();
                        m_textureDirty = true;
                        m_consecutiveFenceTimeouts = 0;
                    } else {
                        // 未达阈值：暂存槽位并排队下一次 paint 重试，
                        // 否则该帧因读指针已推进而永久丢失（静止画面下不再有唤醒源）
                        m_pendingFenceSlot = slot;
                        m_pendingFenceIdx = idx;
                        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
                    }
                }
            } else {
                // No worker-side upload (fallback path): upload on GUI thread
                if (!slot->image.isNull()) {
                    bool uploaded = false;
                    // 优先使用 GpuDecodeTarget
                    if (m_decodeTarget && m_decodeTarget->isReady()) {
                        GLsync fence = m_decodeTarget->uploadPixels(
                            slot->image.constBits(),
                            slot->image.width(), slot->image.height());
                        if (fence) {
                            // 同上下文上传，命令流天然有序，fence 无同步价值，用后即删
                            context()->extraFunctions()->glDeleteSync(fence);
                            m_decodeTarget->swapDisplay();
                            m_textureDirty = true;
                            uploaded = true;
                        }
                    }
                    // GpuDecodeTarget 不可用 → 使用自有回退纹理
                    if (!uploaded) {
                        uploadFallbackTexture(slot->image);
                        m_textureDirty = true;
                    }

                    // 同步纹理尺寸（回退路径也需更新）
                    const QSize imgSize(slot->image.width(), slot->image.height());
                    if (imgSize != m_textureSize) {
                        m_textureSize = imgSize;
                        if (m_renderRect.isEmpty()) updateRenderRect();
                    }
                }
            }
            m_pendingArrivalTs = slot->arrivalTs;
            if ( !slot->remoteSize.isEmpty() ) {
                setRemoteSize(slot->remoteSize);
            }
        }
    }

    static int s_skipCount = 0;
    bool canRenderFrame = true;
    if ( !m_textureDirty ) {
        if ( ++s_skipCount <= 3 || s_skipCount % 300 == 0 )
            qCDebug(lcClientGL) << "paintGL skip #" << s_skipCount << "(m_textureDirty=false)";
        m_needsRepaint.store(false, std::memory_order_release);
        CheckForNewFrameAfterPaint(m_consumedSlot);
        canRenderFrame = false;  // 帧不变，跳过 GL 渲染，但仍绘制光标 OSD
    } else {
        m_textureDirty = false;
        m_needsRepaint.store(false, std::memory_order_release);
        s_skipCount = 0;
    }

    if ( canRenderFrame ) {
        static int s_renderCount = 0;
        ++s_renderCount;

        // 选择纹理：回退纹理优先，否则使用 GpuDecodeTarget
        GLuint texId = (m_fallbackTexture != 0) ? m_fallbackTexture
                                                 : (m_decodeTarget ? m_decodeTarget->displayTexture() : 0);

        if ( s_renderCount <= 3 || s_renderCount % 30 == 0 )
            qCDebug(lcClientGL) << "paintGL rendering #" << s_renderCount
                << "texId:" << texId
                << "size:" << m_textureSize
                << "rect:" << m_renderRect;

        glClear(GL_COLOR_BUFFER_BIT);

        if ( texId != 0 && m_shaderProgram && !m_renderRect.isEmpty() ) {
            // Set viewport to the aspect-ratio-preserving render rectangle
            const qreal dpr = devicePixelRatioF();
            const int rx = static_cast<int>(m_renderRect.x() * dpr);
            const int ry = static_cast<int>((height() - m_renderRect.bottom()) * dpr);
            const int rw = static_cast<int>(m_renderRect.width() * dpr);
            const int rh = static_cast<int>(m_renderRect.height() * dpr);
            glViewport(rx, ry, rw, rh);

            m_shaderProgram->bind();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texId);
            m_shaderProgram->setUniformValue("uTexture", 0);

            QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            m_shaderProgram->release();
        } else if ( s_renderCount <= 3 ) {
            qCDebug(lcClientGL) << "paintGL render skip - texId:" << texId
                << "shader:" << (m_shaderProgram != nullptr)
                << "rect:" << m_renderRect;
        }
        CheckForNewFrameAfterPaint(m_consumedSlot);
    }

    using namespace std::chrono;
    if ( m_pendingArrivalTs.time_since_epoch().count() != 0 ) {
        const auto latencyUs = duration_cast<microseconds>(
            steady_clock::now() - m_pendingArrivalTs).count();
        m_metricsLatencyAccumUs += latencyUs;
        m_metricsLatencyMaxUs = std::max(m_metricsLatencyMaxUs, latencyUs);
        if ( ++m_metricsFrameCount >= GuiConstants::MetricsReportInterval ) {
            const double avgMs = (m_metricsLatencyAccumUs / double(m_metricsFrameCount)) / 1000.0;
            const double maxMs = m_metricsLatencyMaxUs / 1000.0;
            qCDebug(lcClientGL)
                << "end-to-glass avg:" << avgMs << "ms"
                << "max:" << maxMs << "ms"
                << "over" << m_metricsFrameCount << "frames";
            m_metricsFrameCount = 0;
            m_metricsLatencyAccumUs = 0;
            m_metricsLatencyMaxUs = 0;
        }
        m_pendingArrivalTs = {};
    }

    // 帧间间隙修复：绘制完成后检查 TripleBuffer 是否有在绘制期间
    // 到达的新帧。若有则立即 CAS 排队 update()，将等待时间从"下个
    // VSync 周期"缩短到"下个事件循环迭代"。
    CheckForNewFrameAfterPaint(m_consumedSlot);

    // 光标叠加 — GL 原生渲染（在帧渲染之后、swapBuffers 之前）
    if (m_cursorManager && m_cursorManager->hasCursor()) {
        // 同步视口尺寸和渲染矩形（服务端坐标映射用，含 letterbox/pillarbox 偏移）
        m_cursorManager->setViewportSize(size());
        m_cursorManager->setRenderRect(m_renderRect);

        // 懒初始化 GL 资源
        if (!m_cursorGLInit) {
            m_cursorVAO.create();
            m_cursorVBO.create();
            m_cursorVAO.bind();
            m_cursorVBO.bind();
            m_cursorVBO.allocate(16 * sizeof(float));
            m_cursorVBO.setUsagePattern(QOpenGLBuffer::StreamDraw);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
            m_cursorVAO.release();

            glGenTextures(1, &m_cursorTex);
            glBindTexture(GL_TEXTURE_2D, m_cursorTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);

            m_cursorGLInit = true;
        }

        // 无条件上传光标纹理
        {
            const QByteArray& px = m_cursorManager->pixels();
            if (!px.isEmpty()) {
                glBindTexture(GL_TEXTURE_2D, m_cursorTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                    m_cursorManager->width(), m_cursorManager->height(), 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, px.constData());
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // ── 光标清理：用帧纹理重绘旧光标区域，消除像素残留（ghosting）──
        {
            QPoint pos = m_cursorManager->drawPos();
            QRect currentRect(pos.x(), pos.y(),
                              m_cursorManager->width(), m_cursorManager->height());

            if (!canRenderFrame && m_lastCursorPaintRect.isValid()) {

                static int s_cleanupCount = 0;
                ++s_cleanupCount;
                if (s_cleanupCount <= 20 || s_cleanupCount % 60 == 0)
                    qCDebug(lcClientGL) << "[CURSOR-TRACE] GL scissor-cleanup #" << s_cleanupCount
                        << "oldRect:" << m_lastCursorPaintRect
                        << "-> curRect:" << currentRect;

                GLuint frameTex = (m_fallbackTexture != 0) ? m_fallbackTexture
                    : (m_decodeTarget ? m_decodeTarget->displayTexture() : 0);

                if (frameTex != 0 && !m_renderRect.isEmpty()) {
                    // 保存当前 GL 状态
                    GLboolean scissorWasOn = glIsEnabled(GL_SCISSOR_TEST);
                    GLint savedScissor[4];
                    glGetIntegerv(GL_SCISSOR_BOX, savedScissor);

                    const qreal dpr = devicePixelRatioF();
                    const int fbW = static_cast<int>(width() * dpr);
                    const int fbH = static_cast<int>(height() * dpr);

                    // 旧光标区域 → framebuffer 坐标（OpenGL 原点在左下角）
                    GLint sx = static_cast<GLint>(m_lastCursorPaintRect.x() * dpr);
                    GLint sy = static_cast<GLint>(fbH
                        - (m_lastCursorPaintRect.y() + m_lastCursorPaintRect.height()) * dpr);
                    GLsizei sw = static_cast<GLsizei>(m_lastCursorPaintRect.width() * dpr);
                    GLsizei sh = static_cast<GLsizei>(m_lastCursorPaintRect.height() * dpr);

                    // 扩展 1px 补偿浮点取整误差
                    sx = std::max(0, sx - 1);
                    sy = std::max(0, sy - 1);
                    sw = std::min(static_cast<GLsizei>(fbW) - sx, sw + 2);
                    sh = std::min(static_cast<GLsizei>(fbH) - sy, sh + 2);

                    glScissor(sx, sy, sw, sh);
                    glEnable(GL_SCISSOR_TEST);

                    // 清除旧光标像素
                    glClear(GL_COLOR_BUFFER_BIT);

                    // 在旧光标区域重绘帧纹理（scissor 限制仅该区域生效）
                    const int rx = static_cast<int>(m_renderRect.x() * dpr);
                    const int ry = static_cast<int>((height() - m_renderRect.bottom()) * dpr);
                    const int rw = static_cast<int>(m_renderRect.width() * dpr);
                    const int rh = static_cast<int>(m_renderRect.height() * dpr);
                    glViewport(rx, ry, rw, rh);

                    m_shaderProgram->bind();
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, frameTex);
                    m_shaderProgram->setUniformValue("uTexture", 0);
                    {
                        QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);
                        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                    }
                    m_shaderProgram->release();

                    // 恢复 scissor 状态
                    if (!scissorWasOn) {
                        glDisable(GL_SCISSOR_TEST);
                    }
                    glScissor(savedScissor[0], savedScissor[1],
                              savedScissor[2], savedScissor[3]);
                }
            }
            m_lastCursorPaintRect = currentRect;
        }

        // 保存 viewport
        GLint savedVp[4];
        glGetIntegerv(GL_VIEWPORT, savedVp);

        // 全窗口 viewport
        const qreal dpr = devicePixelRatioF();
        glViewport(0, 0, static_cast<GLsizei>(width() * dpr),
                   static_cast<GLsizei>(height() * dpr));

        // 光标 quad (NDC)
        QPoint pos = m_cursorManager->drawPos();
        float cw = static_cast<float>(m_cursorManager->width());
        float ch = static_cast<float>(m_cursorManager->height());
        float w = static_cast<float>(width()), h = static_cast<float>(height());
        float l = (pos.x() / w) * 2.0f - 1.0f;
        float r = ((pos.x() + cw) / w) * 2.0f - 1.0f;
        float t = 1.0f - (pos.y() / h) * 2.0f;
        float b = 1.0f - ((pos.y() + ch) / h) * 2.0f;

        // V 坐标与帧 Quad 一致：顶部 V=0, 底部 V=1（补偿 OpenGL/QImage 原点差异）
        float verts[] = { l,t, 0.f,0.f,  l,b, 0.f,1.f,  r,t, 1.f,0.f,  r,b, 1.f,1.f };

        m_cursorVAO.bind();
        m_cursorVBO.bind();
        m_cursorVBO.write(0, verts, sizeof(verts));

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_shaderProgram->bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_cursorTex);
        m_shaderProgram->setUniformValue("uTexture", 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        static int s_cursorDrawCount = 0;
        if (++s_cursorDrawCount <= 3 || s_cursorDrawCount % 120 == 0)
            qCDebug(lcClientGL) << "cursor GL draw #" << s_cursorDrawCount
                << "tex:" << m_cursorTex << "pos:" << pos
                << "size:" << QSize(cw, ch) << "NDC lrtb:" << l << r << t << b;
        m_shaderProgram->release();

        glDisable(GL_BLEND);
        m_cursorVAO.release();
        glViewport(savedVp[0], savedVp[1], savedVp[2], savedVp[3]);
    }
}

void GLTextureViewport::updateRenderRect() {
    if ( m_textureSize.isEmpty() ) {
        m_renderRect = QRectF();
        return;
    }

    const qreal viewW = width();
    const qreal viewH = height();
    if ( viewW <= 0.0 || viewH <= 0.0 ) {
        m_renderRect = QRectF();
        return;
    }

    // Compute aspect-ratio-preserving rectangle
    // (equivalent to fitInView + KeepAspectRatio)
    const qreal imageAspect = static_cast<qreal>(m_textureSize.width())
                            / m_textureSize.height();
    const qreal viewAspect = viewW / viewH;

    qreal renderW = 0.0;
    qreal renderH = 0.0;
    if ( imageAspect > viewAspect ) {
        // Image wider than viewport: fit to width, letterbox top/bottom
        renderW = viewW;
        renderH = viewW / imageAspect;
    } else {
        // Image taller than viewport: fit to height, pillarbox left/right
        renderH = viewH;
        renderW = viewH * imageAspect;
    }

    const qreal renderX = (viewW - renderW) / 2.0;
    const qreal renderY = (viewH - renderH) / 2.0;

    m_renderRect = QRectF(renderX, renderY, renderW, renderH);
    emit renderRectChanged(m_renderRect);
}

void GLTextureViewport::setRemoteSize(const QSize& size) {
    m_remoteSize = size;
}

QPoint GLTextureViewport::mapToRemote(const QPoint& localPoint) const {
    // localPoint is in widget logical coordinates (Qt handles DPI scaling
    // in event delivery). m_renderRect is also in logical coordinates.
    // No DPR adjustment needed here; only needed in paintGL's glViewport call.
    if ( m_renderRect.isEmpty() ) {
        return localPoint;
    }

    // Use remoteSize if set (for downscaled frames), otherwise texture size
    const QSize targetSize = m_remoteSize.isEmpty() ? m_textureSize : m_remoteSize;
    if ( targetSize.isEmpty() ) {
        return localPoint;
    }

    // Transform: local widget coords -> normalized render rect -> remote coords
    const qreal normX = (localPoint.x() - m_renderRect.x()) / m_renderRect.width();
    const qreal normY = (localPoint.y() - m_renderRect.y()) / m_renderRect.height();

    // Clamp to [0, 1]
    const qreal clampedX = qBound(0.0, normX, 1.0);
    const qreal clampedY = qBound(0.0, normY, 1.0);

    return QPoint(
        static_cast<int>(clampedX * targetSize.width()),
        static_cast<int>(clampedY * targetSize.height())
    );
}

void GLTextureViewport::attachFrameBuffer(TripleBuffer<FrameSlot>* buffer) {
    m_frameBuffer = buffer;
}

#endif // QT_NO_OPENGL
