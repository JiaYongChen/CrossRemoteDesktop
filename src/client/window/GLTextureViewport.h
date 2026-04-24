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
     * @brief Upload a decoded frame to the GPU texture.
     *
     * If the image size matches the current texture, uses glTexSubImage2D
     * (fast path). Otherwise, recreates the texture with glTexImage2D.
     *
     * @param image Decoded frame (any QImage::Format, converted internally)
     */
    void uploadFrame(const QImage& image);

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
};

#endif // QT_NO_OPENGL
