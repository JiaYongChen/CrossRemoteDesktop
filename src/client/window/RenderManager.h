#pragma once

#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtCore/QPoint>
#include <QtCore/QRect>

// Forward declarations
class QWidget;
class GLTextureViewport;

/**
 * @brief RenderManager — lightweight coordinate mapping and scaling service.
 *
 * After the QGraphicsView fallback path was removed, GLTextureViewport is the
 * sole render surface. RenderManager no longer owns any scene/pixmap/cache
 * infrastructure. It provides:
 * - Scale factor management
 * - Coordinate mapping (local <-> remote)
 * - Window resize negotiation
 */
class RenderManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param widget The owning QWidget (ClientRemoteWindow) — used for viewport size queries
     * @param parent Parent object
     */
    explicit RenderManager(QWidget* widget, QObject* parent = nullptr);

    /**
     * @brief Destructor
     */
    ~RenderManager() override;

    // GL viewport binding
    /**
     * @brief Set the GL texture viewport used for coordinate mapping.
     */
    void setGLViewport(GLTextureViewport* viewport);

    /**
     * @brief Check if OpenGL direct texture mode is active (always true after init).
     */
    bool isGLModeActive() const { return m_glModeActive; }

    /**
     * @brief Get the GL viewport (nullptr if not yet bound).
     */
    GLTextureViewport* glViewport() const { return m_glViewport; }

    // Viewport size for scaling calculations
    /**
     * @brief Get the current viewport (widget) size.
     */
    QSize viewportSize() const;

    // Scaling
    /**
     * @brief Set the scale factor.
     * @param factor Scale factor (> 0)
     */
    void setScaleFactor(double factor);

    /**
     * @brief Get the current scale factor.
     */
    double scaleFactor() const { return m_scaleFactor; }

    // Size and coordinate management
    /**
     * @brief Get the remote screen size.
     */
    QSize remoteSize() const { return m_remoteSize; }

    /**
     * @brief Get the scaled size.
     */
    QSize scaledSize() const { return m_scaledSize; }

    /**
     * @brief Map a local point to remote coordinates.
     */
    QPoint mapToRemote(const QPoint& localPoint) const;

    /**
     * @brief Map a remote point to local coordinates.
     */
    QPoint mapFromRemote(const QPoint& remotePoint) const;

    /**
     * @brief Map a local rect to remote coordinates.
     */
    QRect mapToRemote(const QRect& localRect) const;

    /**
     * @brief Map a remote rect to local coordinates.
     */
    QRect mapFromRemote(const QRect& remoteRect) const;

signals:
    /**
     * @brief Emitted when the scale factor changes.
     */
    void scaleFactorChanged(double factor);

    /**
     * @brief Request window resize (for fit-to-window aspect correction).
     */
    void windowResizeRequested(const QSize& size);

public slots:
    /**
     * @brief Handle view resize — recalculates scaled size.
     */
    void onViewResized();

private:
    /**
     * @brief Calculate the scaled size based on remote size and viewport.
     */
    void calculateScaledSize();

    // Member variables
    QWidget* m_widget = nullptr;              ///< Owning widget (for size queries)
    GLTextureViewport* m_glViewport = nullptr; ///< GL texture viewport (not owned)

    QSize m_remoteSize;                       ///< Remote screen size
    QSize m_scaledSize;                       ///< Scaled size

    double m_scaleFactor = 1.0;               ///< Current scale factor
    double m_customScaleFactor = 1.0;         ///< Custom scale factor

    bool m_glModeActive = false;              ///< Whether GL viewport is bound
};
