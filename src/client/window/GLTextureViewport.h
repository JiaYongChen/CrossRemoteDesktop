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
#include <QtGui/qopengl.h>
#include <chrono>

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
     * @brief Check if a texture has been uploaded.
     */
    bool hasTexture() const { return m_textureId != 0; }

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

signals:
    /**
     * @brief Emitted when the viewport is resized and render rect changes.
     */
    void renderRectChanged(const QRectF& rect);

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
     * @brief Clean up OpenGL resources.
     */
    void cleanupGL();

    // OpenGL resources
    GLuint m_textureId = 0;
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

    // PBO double-buffered async upload
    QOpenGLBuffer m_pbo[kPboCount] = {
        QOpenGLBuffer(QOpenGLBuffer::PixelUnpackBuffer),
        QOpenGLBuffer(QOpenGLBuffer::PixelUnpackBuffer),
    };
    int m_currentPbo = 0;
    int m_pboAllocatedBytes = 0;  // current PBO size
    bool m_usePbo = true;

    // Dirty-frame gating for paintGL
    bool m_textureDirty = false;

    // Metrics aggregation
    std::chrono::steady_clock::time_point m_pendingArrivalTs{};
    quint64 m_metricsFrameCount = 0;
    qint64  m_metricsLatencyAccumUs = 0;
    qint64  m_metricsLatencyMaxUs = 0;
    static constexpr quint64 kMetricsReportInterval = 30;  // frames
};

#endif // QT_NO_OPENGL
