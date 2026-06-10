#pragma once

#ifndef QT_NO_OPENGL

#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QtOpenGL/QOpenGLShaderProgram>
#include <QtOpenGL/QOpenGLBuffer>
#include <QtOpenGL/QOpenGLVertexArrayObject>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QImage>
#include <QtCore/QSize>
#include <QtCore/QPoint>
#include <QtCore/QRectF>
#include <QtCore/QTimer>
#include <QtGui/qopengl.h>
#include <chrono>
#include <atomic>

#include "../core/TripleBuffer.h"
#include "../core/FrameSlot.h"

/**
 * @brief OpenGL viewport that renders remote desktop frames via direct texture upload.
 *
 * Replaces QGraphicsView's default QWidget viewport when OpenGL mode is active.
 * Manages an OpenGL texture and renders it as a fullscreen textured quad with
 * aspect-ratio-preserving scaling.
 *
 * Performance advantage over the QGraphicsPixmapItem path:
 * - QPixmap::fromImage() eliminated (~3ms per frame)
 * - QGraphicsScene rendering bypassed
 * - glTexSubImage2D for same-size consecutive frames (sub-millisecond)
 *
 * Thread safety: all public methods must be called from the GUI thread.
 */
class GLTextureViewport : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    struct GLPixelLayout {
        GLint  internalFormat;  // GL_RGB8 / GL_RGBA8
        GLenum format;          // GL_RGB / GL_RGBA
        GLenum type;            // GL_UNSIGNED_BYTE
        int    bytesPerPixel;   // 3 or 4
    };

    /// Pure function mapping QImage::Format to GL upload parameters.
    /// Returns false for unsupported formats — caller must convertedTo() a
    /// supported format before uploading.
    static bool chooseGLFormat(QImage::Format f, GLPixelLayout& out);

    static constexpr int kPboCount = 2;
    /// Pure function: next PBO index in the double-buffered ring.
    static int nextPboIndex(int current) { return (current + 1) % kPboCount; }

    explicit GLTextureViewport(QWidget* parent = nullptr);
    ~GLTextureViewport() override;

    /**
     * @brief Convenience wrapper — equivalent to uploadFrame(image).
     *
     * Provided for API compatibility: setRemoteScreen + setRemoteSize
     * replaces the old RenderManager::setRemoteScreen flow.
     */
    void setRemoteScreen(const QImage& image) { uploadFrame(image); }

    /**
     * @brief Upload a decoded frame to the GPU texture.
     *
     * If the image size matches the current texture, uses glTexSubImage2D
     * (fast path). Otherwise, recreates the texture with glTexImage2D.
     *
     * @param image Decoded frame (any QImage::Format, converted internally)
     */
    void uploadFrame(const QImage& image);

    /// Upload with an attached arrival timestamp to measure end-to-glass latency.
    void uploadFrame(const QImage& image,
                     std::chrono::steady_clock::time_point arrivalTs);

    /// Rebuild surface format with the given VSync setting.
    /// Note: QOpenGLWidget rebuilds its context when format changes, so a
    /// brief frame loss is possible. Caller should re-upload the current
    /// frame after this returns.
    void setVSyncEnabled(bool on);
    bool isVSyncEnabled() const { return m_vsyncEnabled; }

    /**
     * @brief Force an immediate repaint (calls update() internally).
     */
    void renderNow();

    /**
     * @brief 请求重绘（去重版）。线程安全，可从任意线程调用。
     *
     * 仅当上一帧已被 paintGL 消费后才排队新的 update()，
     * 避免 GUI 线程事件队列中堆积冗余的 paint 事件。
     * @return true 表示成功排队了新的 paint 事件。
     */
    bool requestRepaint();

    /**
     * @brief Check if a texture has been uploaded.
     */
    bool hasTexture() const { return m_textureId[0] != 0; }

    /**
     * @brief Get the current texture dimensions.
     */
    QSize textureSize() const { return m_textureSize; }

    /**
     * @brief Get the rectangle where the texture is rendered (in widget coordinates).
     *
     * Accounts for aspect ratio preservation (letterboxing/pillarboxing).
     */
    QRectF renderRect() const;

    // Coordinate mapping (replaces RenderManager's mapToRemote/mapFromRemote in GL mode)

    /**
     * @brief Map a widget-local point to remote desktop coordinates.
     */
    QPoint mapToRemote(const QPoint& localPoint) const;

    /**
     * @brief Map a remote desktop point to widget-local coordinates.
     */
    QPoint mapFromRemote(const QPoint& remotePoint) const;

    /**
     * @brief Set the logical remote screen size (may differ from texture size
     *        when the server downscales frames).
     *
     * Used for coordinate mapping: local->remote maps to this size, not texture size.
     */
    void setRemoteSize(const QSize& size);

    /**
     * @brief Get the logical remote screen size.
     */
    QSize remoteSize() const { return m_remoteSize; }

    /**
     * @brief Attach a TripleBuffer for lock-free frame delivery.
     *
     * Once attached, paintGL() will poll the buffer on each tick and upload
     * any newly committed frame. Set to nullptr to detach.
     */
    void attachFrameBuffer(TripleBuffer<FrameSlot>* buffer);

    /**
     * @brief Upload decoded frame data directly to the GPU texture via PBO.
     *
     * Designed to be called from a worker thread that has made a shared
     * OpenGL context current. Uses m_textureId and m_pbo[] which are
     * automatically shared because the worker's context was created with
     * setShareContext().
     *
     * @param image Decoded frame (any QImage::Format, converted internally).
     * @return GLsync fence for the GUI thread to wait on, or nullptr on failure.
     */
    GLsync uploadFromWorker(const QImage& image);

    /// 生产者背压：返回 paintGL 因 fence 未就绪而连续跳过的帧数。
    /// 生产者可据此降低上传频率，避免 GPU 队列无限积压。
    int consecutiveSkips() const { return m_consecutiveSkips.load(); }

    /**
     * @brief 在窗口隐藏前主动清理 GL 资源，避免 hide() 销毁原生窗口后
     *        makeCurrent() 崩溃（Windows 平台 QWindow 销毁使 GL 上下文不可用）。
     *
     * 应在 closeEvent 中、QWidget::closeEvent（触发 hide()）之前调用。
     * 多次调用安全（幂等），析构函数也会检测并跳过重复清理。
     */
    void cleanupGLResources();

signals:
    /**
     * @brief Emitted when the viewport is resized and render rect changes.
     */
    void renderRectChanged(const QRectF& rect);

    /**
     * @brief Emitted after initializeGL() completes, carrying the GL context
     *        that worker threads can share for cross-thread texture upload.
     */
    void glContextReady(QOpenGLContext* context);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    /**
     * @brief Compile and link the shader program.
     */
    bool initializeShaders();

    /**
     * @brief Create the vertex buffer and VAO for the fullscreen quad.
     */
    void initializeGeometry();

    /**
     * @brief Recalculate the render rectangle based on viewport and texture sizes.
     */
    void updateRenderRect();

    /**
     * @brief Upload texture data without GL context management.
     *
     * Called from uploadFrame() (with makeCurrent/doneCurrent wrap) and from
     * paintGL() (context already current). Sets m_textureDirty=true on success.
     */
    void applyFrame(const QImage& image);

    /**
     * @brief Start or stop the frame-polling timer based on VSync state.
     */
    void configurePollTimer();

    /**
     * @brief Clean up OpenGL resources.
     */
    void cleanupGL();

    /**
     * @brief paintGL 渲染完成后检查 TripleBuffer 是否有在绘制期间到达的新帧。
     *
     * 若有则立即通过 CAS + invokeMethod("update") 排队下一次 paint，
     * 避免帧在缓冲区空等至下一个 VSync 或轮询周期。
     * @param consumedSlot 本次 paintGL 中 getReadSlot 返回的槽位索引
     */
    void CheckForNewFrameAfterPaint(int consumedSlot);

    // OpenGL resources — double-buffered textures to eliminate
    // worker/GUI read-write race on shared GL objects:
    // - Worker writes to texture[1 - displayTexIndex] via shared context
    // - GUI renders from texture[displayTexIndex]
    // - After worker's fence is signaled, swap displayTexIndex atomically
    GLuint m_textureId[2] = {0, 0};
    std::atomic<int> m_displayTexIndex{0};
    QOpenGLShaderProgram* m_shaderProgram = nullptr;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLVertexArrayObject m_vao;

    // Texture state
    QSize m_textureSize;

    // Logical remote screen size (for coordinate mapping)
    QSize m_remoteSize;

    // Cached render rectangle (aspect-ratio-preserving)
    QRectF m_renderRect;

    // GL initialization state
    bool m_glInitialized = false;

    // VSync toggle
    bool m_vsyncEnabled = true;

    // PBO double-buffered async upload (traditional map/unmap path)
    QOpenGLBuffer m_pbo[kPboCount] = {
        QOpenGLBuffer(QOpenGLBuffer::PixelUnpackBuffer),
        QOpenGLBuffer(QOpenGLBuffer::PixelUnpackBuffer),
    };
    int m_currentPbo = 0;        // GUI thread ring-buffer index
    int m_pboAllocatedBytes = 0; // current PBO size
    bool m_usePbo = true;

    // Persistent mapped PBO (GL_ARB_buffer_storage)
    bool m_usePersistentPbo = false;
    void* m_persistentPtr[kPboCount] = {nullptr, nullptr};
    GLuint m_persistentId[kPboCount] = {0, 0};
    int m_sharedPboIndex = 0;    // worker thread ring-buffer index

    void createPersistentPBOs(int size);
    void destroyPersistentPBOs();

    // Dirty-frame gating for paintGL
    bool m_textureDirty = false;

    // 生产者背压：paintGL 因 fence 未就绪而跳过的连续帧数。
    // 生产者 (handleScreenData) 读取此值决定是否降低 GL 上传频率。
    std::atomic<int> m_consecutiveSkips{0};

    /// 防止重复排队 update()：仅在 paintGL 消费完上一帧后才允许新的 paint 事件。
    /// 生产者 (handleScreenData) 写入 true，paintGL 在消费后重置为 false。
    std::atomic<bool> m_needsRepaint{false};

    /// GL 资源是否已通过 cleanupGLResources() 主动清理。
    /// 析构函数检测此标记，避免在原生窗口已销毁后再次调用 makeCurrent() 导致崩溃。
    bool m_glCleanedUp = false;

    // Triple-buffered lock-free frame delivery
    TripleBuffer<FrameSlot>* m_frameBuffer = nullptr;

    /// 当前 paintGL 周期内已消费的 TripleBuffer 槽位索引，-1 表示未消费。
    /// 用于 CheckForNewFrameAfterPaint()：若 peekReady() 与本值不同说明新帧已到达。
    int m_consumedSlot = -1;

    // Frame-polling timer for non-VSync mode
    QTimer* m_pollTimer = nullptr;

    // Metrics aggregation
    std::chrono::steady_clock::time_point m_pendingArrivalTs{};
    quint64 m_metricsFrameCount = 0;
    qint64  m_metricsLatencyAccumUs = 0;
    qint64  m_metricsLatencyMaxUs = 0;
    static constexpr quint64 kMetricsReportInterval = 10;  // frames
};

#endif // QT_NO_OPENGL
