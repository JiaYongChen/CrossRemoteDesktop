#ifndef QT_NO_OPENGL

#include "GpuDecodeTarget.h"
#include "../../common/core/logging/LoggingCategories.h"
#include "../../common/core/config/RenderConfig.h"

#include <QtGui/QOpenGLContext>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QOpenGLExtraFunctions>
#include <cstring>

// GL_ARB_buffer_storage 常量 — 跨平台兼容
#ifndef GL_MAP_PERSISTENT_BIT
#  define GL_MAP_PERSISTENT_BIT  0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#  define GL_MAP_COHERENT_BIT    0x0080
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#  define GL_MAP_INVALIDATE_BUFFER_BIT 0x0004
#endif
#ifndef GL_RGB8
#  define GL_RGB8 0x8051
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#  define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif

// ════════════════ 构造 / 析构 ════════════════

GpuDecodeTarget::GpuDecodeTarget(QOpenGLContext* shareContext)
    : m_shareContext(shareContext) {
}

GpuDecodeTarget::~GpuDecodeTarget() {
    if (m_ready) {
        qCWarning(lcGLViewport) << "GpuDecodeTarget: 未调用 cleanup() 即销毁——资源可能泄漏";
        m_ready = false;
    }
    delete m_workerContext;
    m_workerContext = nullptr;
    delete m_offSurface;
    m_offSurface = nullptr;
}

// ════════════════ 生命周期 ════════════════

bool GpuDecodeTarget::initialize() {
    if (!m_shareContext) {
        qCWarning(lcGLViewport) << "GpuDecodeTarget::initialize() — 无共享上下文";
        return false;
    }

    m_workerContext = new QOpenGLContext();
    m_workerContext->setShareContext(m_shareContext);
    m_workerContext->setFormat(m_shareContext->format());
    if (!m_workerContext->create()) {
        qCWarning(lcGLViewport) << "GpuDecodeTarget: 创建工作线程 GL 上下文失败";
        delete m_workerContext;
        m_workerContext = nullptr;
        return false;
    }

    m_offSurface = new QOffscreenSurface();
    m_offSurface->setFormat(m_workerContext->format());
    m_offSurface->create();

    m_workerContext->makeCurrent(m_offSurface);
    initializeOpenGLFunctions();
    m_workerContext->doneCurrent();

    m_ready = true;
    qCInfo(lcGLViewport) << "GpuDecodeTarget: 初始化完成 — 工作线程 GL 上下文就绪";
    return true;
}

void GpuDecodeTarget::cleanup() {
    m_ready = false;
    if (!m_workerContext) return;

    m_workerContext->makeCurrent(m_offSurface);

    destroyPersistentPBOs();

    if (m_textureId[0] != 0) {
        glDeleteTextures(kTexCount, m_textureId);
        m_textureId[0] = 0;
        m_textureId[1] = 0;
        m_displayTexIndex.store(0);
    }

    for (int i = 0; i < kPboCount; ++i) {
        if (m_pbo[i].isCreated()) m_pbo[i].destroy();
    }
    m_pboAllocatedBytes = 0;
    m_pboWriteIdx = 0;
    m_texWidth = 0;
    m_texHeight = 0;

    m_workerContext->doneCurrent();
    qCInfo(lcGLViewport) << "GpuDecodeTarget: GL 资源已清理";
}

// ════════════════ mapWriteBuffer ════════════════

unsigned char* GpuDecodeTarget::mapWriteBuffer(int width, int height) {
    if (!m_ready) return nullptr;

    ensureTextureSize(width, height);
    if (!ensurePboSize(width, height)) return nullptr;

    m_pendingWidth = width;
    m_pendingHeight = height;

    // 持久映射路径：直接返回已映射指针（零开销）
    if (m_usePersistent && m_persistentPtr[m_pboWriteIdx]) {
        return static_cast<unsigned char*>(m_persistentPtr[m_pboWriteIdx]);
    }

    // 标准 PBO 路径：每次帧均需调用 glMapBufferRange
    QOpenGLBuffer& pbo = m_pbo[m_pboWriteIdx];
    if (!pbo.bind()) return nullptr;

    auto* f = m_workerContext->extraFunctions();
    if (!f) { pbo.release(); return nullptr; }

    void* mapped = f->glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0,
        static_cast<GLsizeiptr>(width) * height * kRGB,
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    pbo.release();

    return static_cast<unsigned char*>(mapped);
}

// ════════════════ commitWriteBuffer ════════════════

GLsync GpuDecodeTarget::commitWriteBuffer() {
    if (!m_ready) return nullptr;

    auto* f = m_workerContext->extraFunctions();
    if (!f) return nullptr;

    const int width = m_pendingWidth;
    const int height = m_pendingHeight;

    // 确保纹理已创建（首次调用时纹理 ID 可能为 0）
    ensureTextureSize(width, height);

    const int targetIdx = 1 - m_displayTexIndex.load();

    if (m_usePersistent && m_persistentPboId[m_pboWriteIdx]) {
        // 持久映射路径：PBO 始终处于映射状态，直接 glTexSubImage2D
        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_persistentPboId[m_pboWriteIdx]);
        glBindTexture(GL_TEXTURE_2D, m_textureId[targetIdx]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    } else {
        // 标准 PBO 路径：解除映射 → glTexSubImage2D
        QOpenGLBuffer& pbo = m_pbo[m_pboWriteIdx];
        if (!pbo.bind()) return nullptr;
        f->glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

        glBindTexture(GL_TEXTURE_2D, m_textureId[targetIdx]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        pbo.release();
    }

    GLsync fence = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    f->glFlush();

    m_pboWriteIdx = (m_pboWriteIdx + 1) % kPboCount;
    return fence;
}

// ════════════════ uploadPixels ════════════════

GLsync GpuDecodeTarget::uploadPixels(const unsigned char* data,
                                      int width, int height) {
    unsigned char* dst = mapWriteBuffer(width, height);
    if (!dst) {
        // PBO 映射失败 → 降级到直接 glTexSubImage2D（CPU 拷贝）
        ensureTextureSize(width, height);
        const int targetIdx = 1 - m_displayTexIndex.load();
        glBindTexture(GL_TEXTURE_2D, m_textureId[targetIdx]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_RGB, GL_UNSIGNED_BYTE, data);

        // 使用当前上下文（Main GL context）创建 fence，而非 Worker context。
        // uploadPixels() 从 paintGL 调用时，当前线程的 GL 上下文是 Main context，
        // Worker context 未 makeCurrent，extraFunctions() 会触发断言。
        auto* ctx = QOpenGLContext::currentContext();
        auto* f = ctx ? ctx->extraFunctions() : nullptr;
        if (!f) return nullptr;
        GLsync fence = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        f->glFlush();
        return fence;
    }

    const int totalBytes = width * height * kRGB;
    std::memcpy(dst, data, static_cast<size_t>(totalBytes));
    return commitWriteBuffer();
}

// ════════════════ swapDisplay / displayTexture ════════════════

void GpuDecodeTarget::swapDisplay() {
    m_displayTexIndex.store(1 - m_displayTexIndex.load(), std::memory_order_release);
}

GLuint GpuDecodeTarget::displayTexture() const {
    return m_textureId[m_displayTexIndex.load(std::memory_order_acquire)];
}

// ════════════════ ensureTextureSize ════════════════

bool GpuDecodeTarget::ensureTextureSize(int width, int height) {
    if (width == m_texWidth && height == m_texHeight)
        return false;

    if (m_textureId[0] != 0) {
        glDeleteTextures(kTexCount, m_textureId);
        m_textureId[0] = 0;
        m_textureId[1] = 0;
    }

    glGenTextures(kTexCount, m_textureId);
    for (int i = 0; i < kTexCount; ++i) {
        glBindTexture(GL_TEXTURE_2D, m_textureId[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8,
                     width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    }

    m_texWidth = width;
    m_texHeight = height;
    m_displayTexIndex.store(0);
    qCDebug(lcGLViewport) << "GpuDecodeTarget: 纹理重建" << width << "x" << height;
    return true;
}

// ════════════════ ensurePboSize ════════════════

bool GpuDecodeTarget::ensurePboSize(int width, int height) {
    const int need = width * height * kRGB;
    if (need == m_pboAllocatedBytes) return true;

    // 首次分配时尝试持久映射 PBO
    if (!m_usePersistent && m_persistentPboId[0] == 0) {
        const auto cfg = RenderConfig::load();
        if (cfg.gl.usePersistentPbo) {
            createPersistentPBOs(need);
            m_pboAllocatedBytes = need;  // 立即标记，防止下一个条件重复创建
        }
    }
    if (m_usePersistent && need != m_pboAllocatedBytes) {
        // 尺寸变更 → 重建持久映射 PBO
        destroyPersistentPBOs();
        createPersistentPBOs(need);
        m_pboAllocatedBytes = need;
    }

    if (!m_usePersistent) {
        // 标准 PBO 路径
        for (int i = 0; i < kPboCount; ++i) {
            if (!m_pbo[i].isCreated()) {
                if (!m_pbo[i].create()) {
                    qCWarning(lcGLViewport) << "GpuDecodeTarget: PBO" << i << "创建失败";
                    return false;
                }
                m_pbo[i].setUsagePattern(QOpenGLBuffer::StreamDraw);
            }
            m_pbo[i].bind();
            m_pbo[i].allocate(nullptr, need);
            m_pbo[i].release();
        }
    }

    m_pboAllocatedBytes = need;
    return true;
}

// ════════════════ 持久映射 PBO ════════════════

void GpuDecodeTarget::createPersistentPBOs(int size) {
    if (!m_workerContext) return;

    const bool hasBufferStorage = m_workerContext->hasExtension(
        QByteArrayLiteral("GL_ARB_buffer_storage"));
    if (!hasBufferStorage) {
        qCInfo(lcGLViewport) << "GpuDecodeTarget: GL_ARB_buffer_storage 不支持——回退到标准 PBO";
        m_usePersistent = false;
        return;
    }

    using GLBufferStorageProc = void (*)(GLenum, GLsizeiptr, const void*, GLbitfield);
    auto glBufferStorageFn = reinterpret_cast<GLBufferStorageProc>(
        m_workerContext->getProcAddress(QByteArrayLiteral("glBufferStorage")));
    if (!glBufferStorageFn) {
        qCInfo(lcGLViewport) << "GpuDecodeTarget: glBufferStorage 不可解析——回退到标准 PBO";
        m_usePersistent = false;
        return;
    }

    auto* f = m_workerContext->extraFunctions();
    if (!f) { m_usePersistent = false; return; }

    for (int i = 0; i < kPboCount; ++i) {
        f->glGenBuffers(1, &m_persistentPboId[i]);
        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_persistentPboId[i]);

        const GLbitfield storageFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glBufferStorageFn(GL_PIXEL_UNPACK_BUFFER, size, nullptr, storageFlags);

        const GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        m_persistentPtr[i] = f->glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, mapFlags);

        if (!m_persistentPtr[i]) {
            qCWarning(lcGLViewport) << "GpuDecodeTarget: 持久 PBO 映射失败，槽位" << i;
            destroyPersistentPBOs();
            m_usePersistent = false;
            return;
        }

        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    m_usePersistent = true;
    qCInfo(lcGLViewport) << "GpuDecodeTarget: 持久 PBO 已创建 —"
                          << size << "bytes x" << kPboCount;
}

void GpuDecodeTarget::destroyPersistentPBOs() {
    auto* f = m_workerContext ? m_workerContext->extraFunctions() : nullptr;
    for (int i = 0; i < kPboCount; ++i) {
        if (m_persistentPboId[i] != 0 && f) {
            f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_persistentPboId[i]);
            if (m_persistentPtr[i]) {
                f->glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                m_persistentPtr[i] = nullptr;
            }
            f->glDeleteBuffers(1, &m_persistentPboId[i]);
            m_persistentPboId[i] = 0;
        } else if (m_persistentPboId[i] != 0) {
            // 无 GL 上下文时仅重置状态（闭包安全）
            m_persistentPtr[i] = nullptr;
            m_persistentPboId[i] = 0;
        }
    }
    m_usePersistent = false;
}

#endif // QT_NO_OPENGL
