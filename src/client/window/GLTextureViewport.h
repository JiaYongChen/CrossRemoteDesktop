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
#include "../managers/DecodeWorker.h"
#include "TextureRingBuffer.h"

/**
 * @brief OpenGL viewport that renders remote desktop frames via texture ring buffer.
 *
 * Receives decoded frames from DecodeWorker via TripleBuffer, uploads them
 * to GPU textures through TextureRingBuffer (3-slot ring with PBO+DMA),
 * and renders the latest ready frame as a fullscreen textured quad with
 * aspect-ratio-preserving scaling.
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

    explicit GLTextureViewport(QWidget* parent = nullptr);
    ~GLTextureViewport() override;

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
     * @brief Check if a texture has been uploaded.
     */
    bool hasTexture() const { return m_ringBuffer.textureSize().isValid(); }

    /**
     * @brief Get the current texture dimensions.
     */
    QSize textureSize() const { return m_ringBuffer.textureSize(); }

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
     * @brief Get the input TripleBuffer for DecodeWorker to write decoded frames.
     */
    TripleBuffer<DecodeWorker::DecodedFrame>* inputBuffer() { return &m_inputBuffer; }

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

public slots:
    /**
     * @brief 由 frameDecoded 信号驱动：从 m_inputBuffer 取出解码帧，
     *        上传到 m_ringBuffer，收割 fence，必要时排队 update()。
     */
    void doPreRender();

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
     * @brief Start or stop the frame-polling timer based on VSync state.
     */
    void configurePollTimer();

    /**
     * @brief Clean up OpenGL resources.
     */
    void cleanupGL();

    /**
     * @brief 保底 fence 收割定时器：周期性调用 pollFences()，
     *        防止 doPreRender 的 update() 在 VSync 开启时因边缘情况丢失。
     */
    void onFallbackTimer();

    // OpenGL resources
    QOpenGLShaderProgram* m_shaderProgram = nullptr;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLVertexArrayObject m_vao;

    // Cached render rectangle (aspect-ratio-preserving)
    QRectF m_renderRect;

    // GL initialization state
    bool m_glInitialized = false;

    // VSync toggle
    bool m_vsyncEnabled = true;

    // Frame-polling timer for non-VSync mode
    QTimer* m_pollTimer = nullptr;

    /// GL 资源是否已通过 cleanupGLResources() 主动清理。
    /// 析构函数检测此标记，避免在原生窗口已销毁后再次调用 makeCurrent() 导致崩溃。
    bool m_glCleanedUp = false;

    // === 新数据管线 ===

    /// 解码帧输入缓冲区（DecodeWorker 写入，doPreRender 读取）
    TripleBuffer<DecodeWorker::DecodedFrame> m_inputBuffer;

    /// 3 槽纹理环缓冲区（PBO + DMA + fence 管理）
    TextureRingBuffer m_ringBuffer;

    /// 保底 fence 收割定时器（16ms）
    QTimer* m_fallbackTimer = nullptr;

    /// 上一帧已渲染的 frameId，用于 paintGL 中跳过旧帧
    quint64 m_lastRenderedFrameId = 0;
};

#endif // QT_NO_OPENGL
