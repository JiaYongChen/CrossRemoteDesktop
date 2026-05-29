#ifndef QT_NO_OPENGL

#include "GLTextureViewport.h"
#include "../../common/core/logging/LoggingCategories.h"
#include "../../common/core/config/RenderConfig.h"

#include <QtGui/QOpenGLExtraFunctions>
#include <QtGui/QOpenGLContext>
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

    // Notify listeners (SessionManager) that the GL context is ready for sharing
    emit glContextReady(context());
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
    destroyPersistentPBOs();
    if ( m_textureId[0] != 0 ) {
        glDeleteTextures(2, m_textureId);
        m_textureId[0] = 0;
        m_textureId[1] = 0;
        m_displayTexIndex.store(0);
    }
    m_vertexBuffer.destroy();
    for (int i = 0; i < kPboCount; ++i) {
        if ( m_pbo[i].isCreated() ) m_pbo[i].destroy();
    }
    m_pboAllocatedBytes = 0;
    m_currentPbo = 0;
    m_sharedPboIndex = 0;
    m_vao.destroy();
    delete m_shaderProgram;
    m_shaderProgram = nullptr;
    m_glInitialized = false;
}

void GLTextureViewport::createPersistentPBOs(int size) {
    // Check extension support via the widget's GL context
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qCWarning(lcGLViewport) << "No current GL context for persistent PBO init";
        m_usePersistentPbo = false;
        return;
    }

    const bool hasBufferStorage = ctx->hasExtension(
        QByteArrayLiteral("GL_ARB_buffer_storage"));
    if (!hasBufferStorage) {
        qCInfo(lcGLViewport) << "GL_ARB_buffer_storage not supported, using traditional PBO";
        m_usePersistentPbo = false;
        return;
    }

    // Resolve glBufferStorage via getProcAddress — it is a GL 4.4+ function
    // not exposed through QOpenGLExtraFunctions.
    using GLBufferStorageProc = void (*)(GLenum, GLsizeiptr, const void*, GLbitfield);
    auto glBufferStorageFn = reinterpret_cast<GLBufferStorageProc>(
        ctx->getProcAddress(QByteArrayLiteral("glBufferStorage")));
    if (!glBufferStorageFn) {
        qCInfo(lcGLViewport) << "glBufferStorage not resolvable, using traditional PBO";
        m_usePersistentPbo = false;
        return;
    }

    auto* f = ctx->extraFunctions();
    if (!f) {
        qCWarning(lcGLViewport) << "Cannot get GL extraFunctions for persistent PBO";
        m_usePersistentPbo = false;
        return;
    }

    for (int i = 0; i < kPboCount; ++i) {
        // Destroy old PBO if it already exists (for resize)
        if (m_persistentId[i] != 0) {
            f->glDeleteBuffers(1, &m_persistentId[i]);
            m_persistentPtr[i] = nullptr;
            m_persistentId[i] = 0;
        }

        f->glGenBuffers(1, &m_persistentId[i]);
        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_persistentId[i]);

        const GLbitfield storageFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glBufferStorageFn(GL_PIXEL_UNPACK_BUFFER, size, nullptr, storageFlags);

        const GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        m_persistentPtr[i] = f->glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, mapFlags);

        if (!m_persistentPtr[i]) {
            qCWarning(lcGLViewport) << "Persistent PBO map failed for slot" << i;
            m_usePersistentPbo = false;
            destroyPersistentPBOs();
            return;
        }

        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    m_pboAllocatedBytes = size;
    m_usePersistentPbo = true;
    qCInfo(lcGLViewport) << "Persistent PBOs created:" << size << "bytes x" << kPboCount;
}

void GLTextureViewport::destroyPersistentPBOs() {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    auto* f = ctx ? ctx->extraFunctions() : nullptr;
    for (int i = 0; i < kPboCount; ++i) {
        if (m_persistentId[i] != 0 && f) {
            f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_persistentId[i]);
            if (m_persistentPtr[i]) {
                f->glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                m_persistentPtr[i] = nullptr;
            }
            f->glDeleteBuffers(1, &m_persistentId[i]);
            m_persistentId[i] = 0;
        } else if (m_persistentId[i] != 0) {
            // Context may be gone during destruction (e.g., app shutdown)
            m_persistentPtr[i] = nullptr;
            m_persistentId[i] = 0;
        }
    }
    m_usePersistentPbo = false;
    m_pboAllocatedBytes = 0;
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
        // 纹理尺寸变更：重建双缓冲纹理
        if ( m_textureId[0] != 0 ) {
            glDeleteTextures(2, m_textureId);
        }

        glGenTextures(2, m_textureId);

        // 用相同的参数和初始数据设置两个纹理
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, m_textureId[i]);

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
        }

        m_textureSize = src->size();
        m_displayTexIndex.store(0);
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
                glBindTexture(GL_TEXTURE_2D, m_textureId[m_displayTexIndex.load()]);
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
                glBindTexture(GL_TEXTURE_2D, m_textureId[m_displayTexIndex.load()]);
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
            glBindTexture(GL_TEXTURE_2D, m_textureId[m_displayTexIndex.load()]);
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

GLsync GLTextureViewport::uploadFromWorker(const QImage& image) {
    // Called from worker thread with a shared GL context already made current.
    // Uploads decoded QImage directly to m_textureId via PBO (DMA async),
    // then inserts a GLsync fence for the GUI thread to wait on in paintGL().
    //
    // This eliminates the PBO memcpy from the GUI thread critical path
    // entirely — the worker thread pays the CPU cost instead, and the GUI
    // thread only draws when the GPU signals the fence is complete.

    if (image.isNull() || image.format() == QImage::Format_Invalid) {
        return nullptr;
    }

    // Determine GL pixel layout from QImage format
    GLPixelLayout layout;
    QImage glImage;
    const QImage* src = nullptr;
    if (chooseGLFormat(image.format(), layout)) {
        src = &image;  // zero-copy fast path
    } else {
        glImage = image.convertedTo(QImage::Format_RGBA8888);
        chooseGLFormat(QImage::Format_RGBA8888, layout);
        src = &glImage;
    }

    const bool sizeChanged = (src->size() != m_textureSize);

    if (sizeChanged) {
        // 纹理尺寸变更：重建双缓冲纹理
        if (m_textureId[0] != 0) {
            glDeleteTextures(2, m_textureId);
        }

        glGenTextures(2, m_textureId);

        // 用相同的参数和初始数据设置两个纹理
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, m_textureId[i]);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH,
                          src->bytesPerLine() / layout.bytesPerPixel);
            glTexImage2D(GL_TEXTURE_2D, 0, layout.internalFormat,
                         src->width(), src->height(), 0,
                         layout.format, layout.type, src->constBits());
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }

        m_textureSize = src->size();
        m_displayTexIndex.store(0);
        // m_textureDirty will be set by paintGL after fence is signaled
    } else {
        const int rowBytes = src->bytesPerLine();
        const int totalBytes = rowBytes * src->height();

        // Lazy init persistent PBOs on first same-size frame
        if (m_usePbo && !m_usePersistentPbo && m_persistentId[0] == 0) {
            const auto cfg = RenderConfig::load();
            m_usePersistentPbo = cfg.gl.usePersistentPbo;
            if (m_usePersistentPbo) {
                createPersistentPBOs(totalBytes);
            }
        }

        // Resize persistent PBOs if frame dimensions changed
        if (m_usePersistentPbo && totalBytes != m_pboAllocatedBytes) {
            destroyPersistentPBOs();
            createPersistentPBOs(totalBytes);
        }

        if (m_usePersistentPbo && m_persistentPtr[m_sharedPboIndex]) {
            // Fast persistent-mapped PBO path: direct memcpy, no alloc/map/unmap
            std::memcpy(m_persistentPtr[m_sharedPboIndex], src->constBits(), size_t(totalBytes));

            auto* f = QOpenGLContext::currentContext()->extraFunctions();
            Q_ASSERT(f);
            f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_persistentId[m_sharedPboIndex]);

            glBindTexture(GL_TEXTURE_2D, m_textureId[1 - m_displayTexIndex.load()]);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, rowBytes / layout.bytesPerPixel);
            // When a PBO is bound, the data pointer is interpreted as a byte
            // offset into the PBO — nullptr means offset 0.
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            src->width(), src->height(),
                            layout.format, layout.type, nullptr);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

            f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);  // unbind
            m_sharedPboIndex = nextPboIndex(m_sharedPboIndex);
        } else if (m_usePbo) {
            // Traditional PBO async upload path (fallback)
            QOpenGLBuffer& pbo = m_pbo[m_currentPbo];
            pbo.bind();
            if (totalBytes != m_pboAllocatedBytes) {
                pbo.allocate(nullptr, totalBytes);  // orphan + realloc
                m_pboAllocatedBytes = totalBytes;
            } else {
                pbo.allocate(nullptr, totalBytes);  // orphan for driver sync
            }
            void* mapped = pbo.map(QOpenGLBuffer::WriteOnly);
            if (mapped) {
                std::memcpy(mapped, src->constBits(), size_t(totalBytes));
                pbo.unmap();
                glBindTexture(GL_TEXTURE_2D, m_textureId[1 - m_displayTexIndex.load()]);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glPixelStorei(GL_UNPACK_ROW_LENGTH,
                              rowBytes / layout.bytesPerPixel);
                // PBO is bound, so nullptr = byte offset 0 into the PBO
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                src->width(), src->height(),
                                layout.format, layout.type, nullptr);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
            pbo.release();
            m_currentPbo = nextPboIndex(m_currentPbo);
        } else {
            // Direct upload (PBO disabled or unavailable)
            glBindTexture(GL_TEXTURE_2D, m_textureId[1 - m_displayTexIndex.load()]);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH,
                          rowBytes / layout.bytesPerPixel);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            src->width(), src->height(),
                            layout.format, layout.type, src->constBits());
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
    }

    // Insert a GPU fence so the GUI thread can wait for the upload to
    // complete before drawing the texture.
    auto* f = QOpenGLContext::currentContext()->extraFunctions();
    Q_ASSERT(f);
    GLsync fence = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    f->glFlush();  // ensure all commands are submitted to the GPU

    return fence;
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
    static int s_paintCount = 0;
    if ( ++s_paintCount <= 3 )
        qCInfo(lcGLViewport) << "paintGL called, frameBuffer:" << (m_frameBuffer != nullptr);
    if ( m_frameBuffer ) {
        FrameSlot* slot = nullptr;
        const int idx = m_frameBuffer->getReadSlot(slot);
        if ( idx >= 0 && slot && !slot->image.isNull() ) {
            if ( slot->uploadFence ) {
                // Worker thread already uploaded this frame to the shared
                // texture via its shared GL context. Check if the GPU has
                // finished the DMA transfer.
                auto* f = context()->extraFunctions();
                Q_ASSERT(f);
                const GLenum result = f->glClientWaitSync(
                    slot->uploadFence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
                static int s_fenceDiagCount = 0;
                ++s_fenceDiagCount;
                if ( s_fenceDiagCount <= 3 || s_fenceDiagCount % 100 == 0 )
                    qCInfo(lcGLViewport) << "paintGL fence #" << s_fenceDiagCount << "result:" << result
                        << (result == GL_ALREADY_SIGNALED ? "SIGNALED" :
                            result == GL_CONDITION_SATISFIED ? "SATISFIED" :
                            result == GL_TIMEOUT_EXPIRED ? "TIMEOUT" : "OTHER");
                if ( result == GL_ALREADY_SIGNALED ||
                     result == GL_CONDITION_SATISFIED ) {
                    // GPU upload complete — texture is ready to draw.
                    f->glDeleteSync(slot->uploadFence);
                    slot->uploadFence = nullptr;
                    // Worker 写入了非显示纹理，GPU 上传完成后交换显示索引
                    m_displayTexIndex.store(1 - m_displayTexIndex.load());
                    m_textureDirty = true;
                    // Update cached size if it changed (first frame or
                    // resolution change handled by worker).
                    if ( slot->image.size() != m_textureSize ) {
                        m_textureSize = slot->image.size();
                    }
                    // Worker 在上传时已设置 m_textureSize 但未调用 updateRenderRect，
                    // 首帧时需要确保 render rect 已计算
                    if ( m_renderRect.isEmpty() && !m_textureSize.isEmpty() ) {
                        updateRenderRect();
                    }
                }
                // else: GL_TIMEOUT_EXPIRED or GL_WAIT_FAILED.
                // Worker hasn't finished DMA yet — skip this frame.
                // paintGL will retry next tick (VSync or poll timer).
            } else {
                // No worker-side upload (fallback path): upload on GUI thread
                // using the stored QImage.
                applyFrame(slot->image);
            }
            m_pendingArrivalTs = slot->arrivalTs;
            if ( !slot->remoteSize.isEmpty() ) {
                setRemoteSize(slot->remoteSize);
            }
        }
    }

    static int s_skipCount = 0;
    if ( !m_textureDirty ) {
        if ( ++s_skipCount <= 3 || s_skipCount % 300 == 0 )
            qCInfo(lcGLViewport) << "paintGL skip #" << s_skipCount << "(m_textureDirty=false)";
        return;  // nothing new to draw
    }
    m_textureDirty = false;
    s_skipCount = 0;  // reset skip counter on successful render

    static int s_renderCount = 0;
    ++s_renderCount;
    if ( s_renderCount <= 3 || s_renderCount % 30 == 0 )
        qCInfo(lcGLViewport) << "paintGL rendering #" << s_renderCount
            << "texId:" << m_textureId[m_displayTexIndex.load()]
            << "size:" << m_textureSize
            << "rect:" << m_renderRect;

    glClear(GL_COLOR_BUFFER_BIT);

    if ( m_textureId[m_displayTexIndex.load()] == 0 || !m_shaderProgram || m_renderRect.isEmpty() ) {
        if ( s_renderCount <= 3 )
            qCWarning(lcGLViewport) << "paintGL render skip - texId:" << m_textureId[m_displayTexIndex.load()]
                << "shader:" << (m_shaderProgram != nullptr)
                << "rect:" << m_renderRect;
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
    glBindTexture(GL_TEXTURE_2D, m_textureId[m_displayTexIndex.load()]);
    m_shaderProgram->setUniformValue("uTexture", 0);

    QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_shaderProgram->release();

    // 简易 paintGL FPS 统计（每 60 帧输出一次）
    static int s_frameCount = 0;
    static auto s_lastFpsTime = std::chrono::steady_clock::now();
    if ( ++s_frameCount >= 60 ) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastFpsTime).count();
        qCInfo(lcRefreshMetrics) << "paintGL FPS:" << (60000.0 / elapsed) << "(" << elapsed << "ms for 60 frames)";
        s_frameCount = 0;
        s_lastFpsTime = now;
    }

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
