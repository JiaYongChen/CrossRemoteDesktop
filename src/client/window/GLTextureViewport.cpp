#ifndef QT_NO_OPENGL

#include "GLTextureViewport.h"
#include "../../common/core/logging/LoggingCategories.h"
#include "../../common/core/config/RenderConfig.h"

#include <algorithm>  // std::max
#include <chrono>
#include <cstring>    // std::memcpy

// GL format constants not guaranteed by all GL headers
#ifndef GL_RGB8
#  define GL_RGB8  0x8051
#endif
#ifndef GL_RGBA8
#  define GL_RGBA8 0x8058
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
    // Stop frame-polling timer
    if ( m_pollTimer ) {
        m_pollTimer->stop();
    }

    // Critical: disconnect aboutToBeDestroyed BEFORE the base destructor chain
    // destroys the underlying QOpenGLContext.
    //
    // Why (Qt 6.9 destruction sequence):
    //   1) This derived destructor body finishes (reaches '}');
    //   2) The compiler invokes ~QOpenGLWidget() — at this point the vtable
    //      has downgraded to QOpenGLWidget (C++ standard);
    //   3) Inside ~QOpenGLWidget() the underlying QOpenGLContext is torn down
    //      and emits aboutToBeDestroyed;
    //   4) Our slot GLTextureViewport::cleanupGL is dispatched via Qt's
    //      QCallableObject::impl, which calls
    //      assertObjectType<GLTextureViewport>(this). qobject_cast goes
    //      through metaObject() — a virtual call — and now returns
    //      QOpenGLWidget::staticMetaObject, so the cast yields nullptr and
    //      the Q_ASSERT fires (qFatal).
    //
    // We cannot rely on ~QObject()'s auto-disconnect because ~QObject() runs
    // at the END of the base destruction chain, long after the signal has
    // already tried to invoke the downgraded slot. The disconnect must happen
    // while the derived vtable is still live — i.e., inside this body.
    if ( QOpenGLContext* ctx = context() ) {
        disconnect(ctx, &QOpenGLContext::aboutToBeDestroyed,
                   this, &GLTextureViewport::cleanupGL);
    }

    // Must make context current before deleting GL resources
    makeCurrent();
    cleanupGL();
    doneCurrent();
}

void GLTextureViewport::initializeGL() {
    initializeOpenGLFunctions();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    if ( !initializeShaders() ) {
        qCCritical(lcGLViewport) << "Failed to initialize shaders";
        return;
    }

    initializeGeometry();

    // Load PBO preference from config; gracefully degrade if create() fails.
    const auto cfg = RenderConfig::load();
    m_usePbo = cfg.gl.usePbo;
    if ( m_usePbo ) {
        for (int i = 0; i < kPboCount; ++i) {
            if ( !m_pbo[i].create() ) {
                qCWarning(lcGLViewport) << "PBO" << i << "create() failed, falling back to direct upload";
                m_usePbo = false;
                break;
            }
            m_pbo[i].setUsagePattern(QOpenGLBuffer::StreamDraw);
        }
    }

    m_glInitialized = true;

    // Prepare for context loss recovery
    connect(context(), &QOpenGLContext::aboutToBeDestroyed,
            this, &GLTextureViewport::cleanupGL);

    qCInfo(lcGLViewport) << "OpenGL initialized:"
        << "vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR))
        << "renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
        << "version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));
}

bool GLTextureViewport::initializeShaders() {
    m_shaderProgram = new QOpenGLShaderProgram(this);

    if ( !m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertexShaderSource) ) {
        qCCritical(lcGLViewport) << "Vertex shader compilation failed:"
            << m_shaderProgram->log();
        return false;
    }

    if ( !m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragmentShaderSource) ) {
        qCCritical(lcGLViewport) << "Fragment shader compilation failed:"
            << m_shaderProgram->log();
        return false;
    }

    if ( !m_shaderProgram->link() ) {
        qCCritical(lcGLViewport) << "Shader program link failed:"
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
    if ( m_textureId != 0 ) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_vertexBuffer.destroy();
    for (int i = 0; i < kPboCount; ++i) {
        if ( m_pbo[i].isCreated() ) m_pbo[i].destroy();
    }
    m_pboAllocatedBytes = 0;
    m_currentPbo = 0;
    m_vao.destroy();
    delete m_shaderProgram;
    m_shaderProgram = nullptr;
    m_glInitialized = false;
}

void GLTextureViewport::applyFrame(const QImage& image) {
    // Core texture upload without GL context management.
    // Called from uploadFrame() (with makeCurrent/doneCurrent) and from
    // paintGL() (context already current). Sets m_textureDirty on success.

    if ( image.isNull() || image.format() == QImage::Format_Invalid ) {
        return;
    }

    // Determine GL pixel layout from QImage format — avoids a mandatory
    // RGBA8888 CPU copy for common formats like RGB888 (JPEG decode output).
    GLPixelLayout layout;
    QImage glImage;
    const QImage* src = nullptr;
    if ( chooseGLFormat(image.format(), layout) ) {
        src = &image;  // zero-copy fast path
    } else {
        glImage = image.convertedTo(QImage::Format_RGBA8888);
        chooseGLFormat(QImage::Format_RGBA8888, layout);
        src = &glImage;
    }

    const bool sizeChanged = (src->size() != m_textureSize);

    if ( sizeChanged ) {
        // Texture size changed: recreate texture
        if ( m_textureId != 0 ) {
            glDeleteTextures(1, &m_textureId);
        }

        glGenTextures(1, &m_textureId);
        glBindTexture(GL_TEXTURE_2D, m_textureId);

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Allocate and upload texture data
        // QImage always pads rows to 4-byte alignment
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,
                      src->bytesPerLine() / layout.bytesPerPixel);
        glTexImage2D(GL_TEXTURE_2D, 0, layout.internalFormat,
                     src->width(), src->height(), 0,
                     layout.format, layout.type, src->constBits());
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        m_textureSize = src->size();
        updateRenderRect();

        qCDebug(lcGLViewport) << "Texture created:" << m_textureSize;
    } else {
        const int rowBytes = src->bytesPerLine();
        const int totalBytes = rowBytes * src->height();

        if ( m_usePbo ) {
            // Fast async PBO path
            QOpenGLBuffer& pbo = m_pbo[m_currentPbo];
            pbo.bind();
            if ( totalBytes != m_pboAllocatedBytes ) {
                pbo.allocate(nullptr, totalBytes);  // orphan + realloc
                m_pboAllocatedBytes = totalBytes;
            } else {
                pbo.allocate(nullptr, totalBytes);  // orphan for driver sync
            }
            void* mapped = pbo.map(QOpenGLBuffer::WriteOnly);
            if ( mapped ) {
                std::memcpy(mapped, src->constBits(), size_t(totalBytes));
                pbo.unmap();
                glBindTexture(GL_TEXTURE_2D, m_textureId);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, rowBytes / layout.bytesPerPixel);
                // When a PBO is bound, the data pointer is interpreted as a byte
                // offset into the PBO — nullptr means offset 0.
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                src->width(), src->height(),
                                layout.format, layout.type, nullptr);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            } else {
                qCWarning(lcGLViewport) << "PBO map failed, falling back to direct upload once";
                glBindTexture(GL_TEXTURE_2D, m_textureId);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, rowBytes / layout.bytesPerPixel);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                src->width(), src->height(),
                                layout.format, layout.type, src->constBits());
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
            pbo.release();
            m_currentPbo = nextPboIndex(m_currentPbo);
        } else {
            // Direct upload (PBO disabled or unavailable)
            glBindTexture(GL_TEXTURE_2D, m_textureId);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, rowBytes / layout.bytesPerPixel);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            src->width(), src->height(),
                            layout.format, layout.type, src->constBits());
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
    }

    m_textureDirty = true;
}

void GLTextureViewport::uploadFrame(const QImage& image,
                                    std::chrono::steady_clock::time_point arrivalTs) {
    m_pendingArrivalTs = arrivalTs;
    uploadFrame(image);
}

void GLTextureViewport::uploadFrame(const QImage& image) {
    if ( image.isNull() || image.format() == QImage::Format_Invalid ) {
        qCWarning(lcGLViewport) << "Received null or invalid image, skipping upload";
        return;
    }

    if ( !m_glInitialized ) {
        qCWarning(lcGLViewport) << "GL not initialized, skipping upload";
        return;
    }

    makeCurrent();
    applyFrame(image);
    doneCurrent();

    // Schedule repaint
    update();
}

void GLTextureViewport::renderNow() {
    update();
}

void GLTextureViewport::resizeGL(int w, int h) {
    Q_UNUSED(w)
    Q_UNUSED(h)
    updateRenderRect();
}

void GLTextureViewport::paintGL() {
    // Check triple buffer for new frames (lock-free, atomic read)
    if ( m_frameBuffer ) {
        FrameSlot* slot = nullptr;
        const int idx = m_frameBuffer->getReadSlot(slot);
        if ( idx >= 0 && slot && !slot->image.isNull() ) {
            applyFrame(slot->image);
            m_pendingArrivalTs = slot->arrivalTs;
            if ( !slot->remoteSize.isEmpty() ) {
                setRemoteSize(slot->remoteSize);
            }
        }
    }

    if ( !m_textureDirty ) {
        return;  // nothing new to draw
    }
    m_textureDirty = false;

    glClear(GL_COLOR_BUFFER_BIT);

    if ( m_textureId == 0 || !m_shaderProgram || m_renderRect.isEmpty() ) {
        return;
    }

    // Set viewport to the aspect-ratio-preserving render rectangle
    const qreal dpr = devicePixelRatioF();
    const int rx = static_cast<int>(m_renderRect.x() * dpr);
    const int ry = static_cast<int>((height() - m_renderRect.bottom()) * dpr);
    const int rw = static_cast<int>(m_renderRect.width() * dpr);
    const int rh = static_cast<int>(m_renderRect.height() * dpr);
    glViewport(rx, ry, rw, rh);

    m_shaderProgram->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    m_shaderProgram->setUniformValue("uTexture", 0);

    QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_shaderProgram->release();

    using namespace std::chrono;
    if ( m_pendingArrivalTs.time_since_epoch().count() != 0 ) {
        const auto latencyUs = duration_cast<microseconds>(
            steady_clock::now() - m_pendingArrivalTs).count();
        m_metricsLatencyAccumUs += latencyUs;
        m_metricsLatencyMaxUs = std::max(m_metricsLatencyMaxUs, latencyUs);
        if ( ++m_metricsFrameCount >= kMetricsReportInterval ) {
            const double avgMs = (m_metricsLatencyAccumUs / double(m_metricsFrameCount)) / 1000.0;
            const double maxMs = m_metricsLatencyMaxUs / 1000.0;
            qCInfo(lcRefreshMetrics)
                << "end-to-glass avg:" << avgMs << "ms"
                << "max:" << maxMs << "ms"
                << "over" << m_metricsFrameCount << "frames";
            m_metricsFrameCount = 0;
            m_metricsLatencyAccumUs = 0;
            m_metricsLatencyMaxUs = 0;
        }
        m_pendingArrivalTs = {};
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
    qCInfo(lcGLViewport) << "VSync toggled:" << (on ? "ON" : "OFF");
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
