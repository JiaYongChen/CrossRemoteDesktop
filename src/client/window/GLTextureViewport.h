#pragma once

#include <atomic>
#include <chrono>

#include <QtCore/QPoint>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtGui/QImage>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/qopengl.h>
#include <QtOpenGL/QOpenGLBuffer>
#include <QtOpenGL/QOpenGLShaderProgram>
#include <QtOpenGL/QOpenGLVertexArrayObject>
#include <QtOpenGLWidgets/QOpenGLWidget>

#include "client/core/FrameSlot.h"
#include "client/core/TripleBuffer.h"

class CursorManager;
class GpuDecodeTarget;

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
    explicit GLTextureViewport(QWidget* parent = nullptr);
    ~GLTextureViewport() override;

    /**
     * @brief Get the GpuDecodeTarget (created internally in initializeGL), or nullptr.
     */
    GpuDecodeTarget* decodeTarget() const { return m_decodeTarget; }

    /**
     * @brief 强制下一帧 paintGL 渲染，无视 m_textureDirty 状态。
     *
     * 用于窗口状态切换（全屏↔窗口）后确保纹理在新视口下重绘。
     */
    void forceRepaint();

    /**
     * @brief 请求重绘（去重版）。线程安全，可从任意线程调用。
     *
     * 仅当上一帧已被 paintGL 消费后才排队新的 update()，
     * 避免 GUI 线程事件队列中堆积冗余的 paint 事件。
     * @return true 表示成功排队了新的 paint 事件。
     */
    bool requestRepaint();

    // Coordinate mapping (local widget ↔ remote desktop coordinates)

    /**
     * @brief Map a widget-local point to remote desktop coordinates.
     */
    QPoint mapToRemote(const QPoint& localPoint) const;

    /**
     * @brief Attach a TripleBuffer for lock-free frame delivery.
     *
     * 帧驱动为事件式：解码线程每提交一帧调用 requestRepaint() 排队 paint，
     * paintGL 消费缓冲中最新的已提交帧。传入 nullptr 可解除挂载。
     */
    void attachFrameBuffer(TripleBuffer<FrameSlot>* buffer);

    /// 设置 CursorManager（用于 GL 视图上叠加渲染远程光标）
    void setCursorManager(CursorManager* mgr) { m_cursorManager = mgr; }

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

    /**
     * @brief GL 资源即将销毁（QOpenGLWidget 上下文重建/析构时）。
     *
     * 外部持有 GpuDecodeTarget 裸指针的组件必须在此信号触发后、cleanupGL 执行前
     * 停用该指针（停止管线/清空引用），避免悬空指针 use-after-free 崩溃。
     */
    void glResourcesAboutToBeDestroyed();

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
     * @brief Set the logical remote screen size (may differ from texture size
     *        when the server downscales frames).
     *
     * Used for coordinate mapping: local->remote maps to this size, not texture size.
     * 仅由 paintGL 在消费帧时调用。
     */
    void setRemoteSize(const QSize& size);

    /**
     * @brief Clean up OpenGL resources.
     */
    void cleanupGL();

    /**
     * @brief 确保回退纹理存在且尺寸匹配（GpuDecodeTarget 不可用时使用）。
     */
    void ensureFallbackTexture(int width, int height);

    /**
     * @brief 将 QImage 数据上传到回退纹理。
     */
    void uploadFallbackTexture(const QImage& image);

    /**
     * @brief paintGL 渲染完成后检查 TripleBuffer 是否有在绘制期间到达的新帧。
     *
     * 若有则立即通过 CAS + invokeMethod("update") 排队下一次 paint，
     * 避免帧在缓冲区空等至下一个 VSync 周期。
     * @param consumedSlot 本次 paintGL 中 getReadSlot 返回的槽位索引
     */
    void CheckForNewFrameAfterPaint(int consumedSlot);

    // OpenGL resources — managed by GpuDecodeTarget
    QOpenGLShaderProgram* m_shaderProgram = nullptr;
    GpuDecodeTarget* m_decodeTarget = nullptr;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLVertexArrayObject m_vao;

    // Texture state
    QSize m_textureSize;

    // 回退纹理（当 GpuDecodeTarget 不可用时）
    GLuint m_fallbackTexture = 0;
    QSize m_fallbackTexSize;

    // Logical remote screen size (for coordinate mapping)
    QSize m_remoteSize;

    // Cached render rectangle (aspect-ratio-preserving)
    QRectF m_renderRect;

    // GL initialization state
    bool m_glInitialized = false;

    // Dirty-frame gating for paintGL
    bool m_textureDirty = false;

    /// 防止重复排队 update()：仅在 paintGL 消费完上一帧后才允许新的 paint 事件。
    /// 由 requestRepaint()（解码线程经 DecodeWorker 调用）写入 true，
    /// paintGL 在消费后重置为 false。
    std::atomic<bool> m_needsRepaint{false};

    // Triple-buffered lock-free frame delivery
    TripleBuffer<FrameSlot>* m_frameBuffer = nullptr;

    /// 当前 paintGL 周期内已消费的 TripleBuffer 槽位索引，-1 表示未消费。
    /// 用于 CheckForNewFrameAfterPaint()：若 peekReady() 与本值不同说明新帧已到达。
    int m_consumedSlot = -1;

    /// fence 超时重试：TIMEOUT（未达强制放弃阈值）时暂存未就绪的槽位，
    /// 下一次 paintGL 优先重查该 fence（期间有新帧则丢弃旧 fence，latest-wins）。
    /// 若无此机制，getReadSlot 已推进读指针的槽位将永远无法重入 fence 分支，
    /// 静止画面下最后一帧会永久丢失。
    FrameSlot* m_pendingFenceSlot = nullptr;
    int m_pendingFenceIdx = -1;

    /// 连续 fence 超时计数（每实例独立，避免多窗口 share-gate 污染）。
    /// ≥5 次时强制放弃 fence 并 swapDisplay（防止画面永久卡死）。
    int m_consecutiveFenceTimeouts = 0;

    // Metrics aggregation
    std::chrono::steady_clock::time_point m_pendingArrivalTs{};
    quint64 m_metricsFrameCount = 0;
    qint64  m_metricsLatencyAccumUs = 0;
    qint64  m_metricsLatencyMaxUs = 0;

    // 远程光标叠加渲染
    CursorManager* m_cursorManager = nullptr;
    QOpenGLBuffer m_cursorVBO{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_cursorVAO;
    GLuint m_cursorTex = 0;
    bool m_cursorGLInit = false;

    // 光标清理：追踪上一次 paint 时的光标屏幕空间矩形，
    // 用于检测光标移动并触发旧光标区域清理（glClear + 帧重绘）
    QRect m_lastCursorPaintRect;
};

