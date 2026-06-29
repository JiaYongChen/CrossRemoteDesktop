#ifndef QT_NO_OPENGL

#include "GLTextureViewport.h"
#include "../decode/GpuDecodeTarget.h"
#include "../../common/core/logging/LoggingCategories.h"
#include "../../common/core/config/RenderConfig.h"
#include "CursorManager.h"

#include <QtGui/QOpenGLExtraFunctions>
#include <QtGui/QOpenGLContext>
#include <QtGui/QPainter>
#include <algorithm>  // std::max
#include <chrono>

// GL format constants not guaranteed by all GL headers
#ifndef GL_RGB8
#  define GL_RGB8  0x8051
#endif
#ifndef GL_RGBA8
#  define GL_RGBA8 0x8058
#endif

// GL fence-sync constants not guaranteed by all GL headers
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#  define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#  define GL_SYNC_FLUSH_COMMANDS_BIT    0x00000001
#endif
#ifndef GL_ALREADY_SIGNALED
#  define GL_ALREADY_SIGNALED           0x911A
#endif
#ifndef GL_CONDITION_SATISFIED
#  define GL_CONDITION_SATISFIED        0x911C
#endif
#ifndef GL_TIMEOUT_EXPIRED
#  define GL_TIMEOUT_EXPIRED            0x911B
#endif
#ifndef GL_WAIT_FAILED
#  define GL_WAIT_FAILED                0x911D
#endif

// GL_ARB_buffer_storage / GL 4.4+ constants not guaranteed by all GL headers
#ifndef GL_MAP_PERSISTENT_BIT
#  define GL_MAP_PERSISTENT_BIT  0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#  define GL_MAP_COHERENT_BIT    0x0080
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#  define GL_MAP_INVALIDATE_BUFFER_BIT 0x0004
#endif

bool GLTextureViewport::chooseGLFormat(QImage::Format f, GLPixelLayout& out) {
    switch ( f ) {
        case QImage::Format_RGB888:
            out = {GL_RGB8,  GL_RGB,  GL_UNSIGNED_BYTE, 3};
            return true;
        case QImage::Format_RGBA8888:
        case QImage::Format_RGBA8888_Premultiplied:
            out = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
            return true;
        default:
            return false;
    }
}

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
    , m_vertexBuffer(QOpenGLBuffer::VertexBuffer)
    , m_pollTimer(new QTimer(this)) {
    // Request OpenGL 3.3 Core profile
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    const auto cfg = RenderConfig::load();
    m_vsyncEnabled = cfg.gl.vsyncEnabled;
    format.setSwapInterval(m_vsyncEnabled ? 1 : 0);
    setFormat(format);

    // Frame-polling timer — only active when VSync is off and a frame buffer
    // is attached. 16ms (60fps) is the default cadence.
    m_pollTimer->setInterval(16);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() { update(); });
}

GLTextureViewport::~GLTextureViewport() {
    if (m_pollTimer) {
        m_pollTimer->stop();
    }

    // 在 vtable 降级前断开 aboutToBeDestroyed 信号，避免基类析构时 Qt 断言崩溃
    if (QOpenGLContext* ctx = context()) {
        disconnect(ctx, &QOpenGLContext::aboutToBeDestroyed,
                   this, &GLTextureViewport::cleanupGL);
    }

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

    // Notify listeners (SessionManager) that the GL context is ready for sharing
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

void GLTextureViewport::attachDecodeTarget(GpuDecodeTarget* target) {
    m_decodeTarget = target;
}

bool GLTextureViewport::hasTexture() const {
    return (m_decodeTarget && m_decodeTarget->displayTexture() != 0) || m_fallbackTexture != 0;
}

void GLTextureViewport::setRemoteScreen(const QImage& image) {
    if (!m_decodeTarget || image.isNull() || image.format() == QImage::Format_Invalid) {
        return;
    }

    if (!m_glInitialized) {
        qCDebug(lcClientGL) << "GL not initialized, skipping setRemoteScreen";
        return;
    }

    makeCurrent();
    GLsync fence = m_decodeTarget->uploadPixels(image.constBits(), image.width(), image.height()); Q_UNUSED(fence);
    m_decodeTarget->swapDisplay();
    m_textureDirty = true;

    // 同步纹理尺寸
    const QSize newSize(image.width(), image.height());
    if (newSize != m_textureSize) {
        m_textureSize = newSize;
        if (m_renderRect.isEmpty()) updateRenderRect();
    }

    doneCurrent();
    update();
}

void GLTextureViewport::renderNow() {
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
    // 将帧间等待时间从"下一个 VSync/轮询"缩短到"下一个事件循环迭代"。
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
        return;
    }

    // GpuDecodeTarget 未就绪或初始化失败，跳过绘制
    if (!m_decodeTarget) {
        return;
    }

    // Check triple buffer for new frames (lock-free, atomic read)
    static int s_paintCount = 0;
    if ( ++s_paintCount <= 3 )
        qCDebug(lcClientGL) << "paintGL called, frameBuffer:" << (m_frameBuffer != nullptr);
    m_consumedSlot = -1;  // 每次 paintGL 重置
    if ( m_frameBuffer ) {
        FrameSlot* slot = nullptr;
        const int idx = m_frameBuffer->getReadSlot(slot);
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
                // timeout=2ms: 给 GPU DMA 上传一个短暂的完成窗口，
                // 避免零超时导致差几微秒的帧被直接丢弃。
                const GLenum result = f->glClientWaitSync(
                    slot->uploadFence, GL_SYNC_FLUSH_COMMANDS_BIT, 5000000 /* 5ms */);
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

                static int s_consecutiveFenceTimeouts = 0;
                if ( result == GL_ALREADY_SIGNALED ||
                     result == GL_CONDITION_SATISFIED ) {
                    s_consecutiveFenceTimeouts = 0;
                    // GPU upload complete — texture is ready to draw.
                    m_consecutiveSkips.store(0, std::memory_order_relaxed);
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
                    ++s_consecutiveFenceTimeouts;
                    if ( s_consecutiveFenceTimeouts >= 5 ) {
                        // 连续 5 次超时（~80ms）：强制放弃 fence，避免画面卡死。
                        // 可能显示部分上传的纹理，但远好于冻结。
                        qCWarning(lcClientGL) << "paintGL: force-skipping stuck fence after"
                                               << s_consecutiveFenceTimeouts << "timeouts";
                        f->glDeleteSync(slot->uploadFence);
                        slot->uploadFence = nullptr;
                        m_decodeTarget->swapDisplay();
                        m_textureDirty = true;
                        m_consecutiveSkips.store(0, std::memory_order_relaxed);
                        s_consecutiveFenceTimeouts = 0;
                    } else {
                        // 增递跳过计数器，生产者可据此降低上传频率
                        m_consecutiveSkips.fetch_add(1, std::memory_order_relaxed);
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

    // FPS 统计（EMA 平滑，基于渲染时刻——用户实际看到的帧率）
    const auto now = std::chrono::steady_clock::now();
    if (m_lastPaintTime.time_since_epoch().count() != 0) {
        const double instant = std::chrono::duration<double>(now - m_lastPaintTime).count();
        if (m_smoothedFrameDuration == 0.0) {
            m_smoothedFrameDuration = instant;
        } else {
            m_smoothedFrameDuration = kFpsAlpha * instant + (1.0 - kFpsAlpha) * m_smoothedFrameDuration;
        }
        m_currentFPS = (m_smoothedFrameDuration > 0.0) ? (1.0 / m_smoothedFrameDuration) : 0.0;
    }
    m_lastPaintTime = now;

    using namespace std::chrono;
    if ( m_pendingArrivalTs.time_since_epoch().count() != 0 ) {
        const auto latencyUs = duration_cast<microseconds>(
            steady_clock::now() - m_pendingArrivalTs).count();
        m_metricsLatencyAccumUs += latencyUs;
        m_metricsLatencyMaxUs = std::max(m_metricsLatencyMaxUs, latencyUs);
        if ( ++m_metricsFrameCount >= kMetricsReportInterval ) {
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
    // VSync/轮询周期"缩短到"下个事件循环迭代"。
    CheckForNewFrameAfterPaint(m_consumedSlot);

    // 光标叠加 — GL 原生渲染（在帧渲染之后、swapBuffers 之前）
    if (m_cursorManager && m_cursorManager->hasCursor()) {
        // 同步视口尺寸（服务端坐标映射用）
        m_cursorManager->setViewportSize(size());

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

        // 无条件上传光标纹理（确保形状不变、仅位置变化时纹理有效）
        {
            const QByteArray& px = m_cursorManager->pixels();
            if (!px.isEmpty()) {
                static int s_pxDiag = 0;
                if (++s_pxDiag <= 3) {
                    const uchar* d = reinterpret_cast<const uchar*>(px.constData());
                    int nz = 0, sz = px.size();
                    for (int i = 0; i < sz; ++i) if (d[i] != 0) ++nz;
                    qCDebug(lcClientGL) << "cursor upload" << m_cursorManager->width()
                        << "x" << m_cursorManager->height() << "pixels:" << sz << "bytes"
                        << "nonZero:" << nz << "first8:" << Qt::hex << d[0] << d[1]
                        << d[2] << d[3] << d[4] << d[5] << d[6] << d[7];
                }
                glBindTexture(GL_TEXTURE_2D, m_cursorTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                    m_cursorManager->width(), m_cursorManager->height(), 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, px.constData());
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // 保存 viewport
        GLint savedVp[4];
        glGetIntegerv(GL_VIEWPORT, savedVp);

        // 全窗口 viewport
        const qreal dpr = devicePixelRatioF();
        glViewport(0, 0, static_cast<GLsizei>(width() * dpr),
                   static_cast<GLsizei>(height() * dpr));

        // ── 诊断：用 scissor clear 替代 glDrawArrays 光标 quad ──
        QPoint pos = m_cursorManager->drawPos();
        int cw = m_cursorManager->width();
        int ch = m_cursorManager->height();
        const qreal dpr2 = devicePixelRatioF();
        // scissor 用窗口坐标 (bottom-left origin)
        int sx = static_cast<int>(pos.x() * dpr2);
        int sy = static_cast<int>((height() - pos.y() - ch) * dpr2);
        int sw = static_cast<int>(cw * dpr2);
        int sh = static_cast<int>(ch * dpr2);
        if (sw > 0 && sh > 0) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(sx, sy, sw, sh);
            // 交替红/绿色便于跟踪
            static int s_scissorCursorCount = 0;
            bool red = (++s_scissorCursorCount % 2);
            glClearColor(red ? 1.0f : 0.0f, red ? 0.0f : 1.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glDisable(GL_SCISSOR_TEST);
        }
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

QRectF GLTextureViewport::renderRect() const {
    return m_renderRect;
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

QPoint GLTextureViewport::mapFromRemote(const QPoint& remotePoint) const {
    if ( m_renderRect.isEmpty() ) {
        return remotePoint;
    }

    const QSize targetSize = m_remoteSize.isEmpty() ? m_textureSize : m_remoteSize;
    if ( targetSize.isEmpty() ) {
        return remotePoint;
    }

    // Transform: remote coords -> normalized -> local widget coords
    const qreal normX = static_cast<qreal>(remotePoint.x()) / targetSize.width();
    const qreal normY = static_cast<qreal>(remotePoint.y()) / targetSize.height();

    return QPoint(
        static_cast<int>(normX * m_renderRect.width() + m_renderRect.x()),
        static_cast<int>(normY * m_renderRect.height() + m_renderRect.y())
    );
}

void GLTextureViewport::setVSyncEnabled(bool on) {
    if ( m_vsyncEnabled == on ) return;
    m_vsyncEnabled = on;
    QSurfaceFormat f = format();
    f.setSwapInterval(on ? 1 : 0);
    setFormat(f);
    qCInfo(lcClientGL) << "VSync toggled:" << (on ? "ON" : "OFF");
    configurePollTimer();
    update();
}

void GLTextureViewport::attachFrameBuffer(TripleBuffer<FrameSlot>* buffer) {
    m_frameBuffer = buffer;
    configurePollTimer();
}

void GLTextureViewport::configurePollTimer() {
    if ( !m_pollTimer ) return;

    // With VSync enabled, paintGL is driven by the compositor — no timer needed.
    // With VSync disabled and a frame buffer attached, poll at ~60fps.
    const bool needPolling = !m_vsyncEnabled && (m_frameBuffer != nullptr);
    if ( needPolling && !m_pollTimer->isActive() ) {
        m_pollTimer->start();
    } else if ( !needPolling && m_pollTimer->isActive() ) {
        m_pollTimer->stop();
    }
}

#endif // QT_NO_OPENGL
