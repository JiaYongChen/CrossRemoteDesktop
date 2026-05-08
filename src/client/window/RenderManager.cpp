#include "RenderManager.h"
#ifndef QT_NO_OPENGL
#include "GLTextureViewport.h"
#endif
#include "../../common/core/logging/LoggingCategories.h"
#include <QtWidgets/QWidget>
#include <cmath>

RenderManager::RenderManager(QWidget* widget, QObject* parent)
    : QObject(parent)
    , m_widget(widget)
    , m_remoteSize(1024, 768)
    , m_scaledSize(1024, 768) {
}

RenderManager::~RenderManager() {
    // GLTextureViewport is NOT owned by RenderManager; it's owned by ClientRemoteWindow.
    // No cleanup needed here.
}

void RenderManager::setGLViewport(GLTextureViewport* viewport) {
#ifndef QT_NO_OPENGL
    m_glViewport = viewport;
    m_glModeActive = (viewport != nullptr);
    qCInfo(lcRenderManager) << "GL viewport bound:" << (m_glModeActive ? "yes" : "no");
#else
    Q_UNUSED(viewport)
    m_glModeActive = false;
#endif
}

QSize RenderManager::viewportSize() const {
    if ( m_widget ) {
        return m_widget->size();
    }
    return QSize(1024, 768);
}

void RenderManager::setScaleFactor(double factor) {
    if ( factor <= 0.0 ) {
        qCWarning(lcRenderManager) << "RenderManager::setScaleFactor() - Invalid scale factor:" << factor;
        return;
    }
    m_customScaleFactor = factor;
    if ( !qFuzzyCompare(m_scaleFactor, m_customScaleFactor) ) {
        m_scaleFactor = m_customScaleFactor;
        emit scaleFactorChanged(m_scaleFactor);
    }
}

QPoint RenderManager::mapToRemote(const QPoint& localPoint) const {
#ifndef QT_NO_OPENGL
    if ( m_glModeActive && m_glViewport ) {
        return m_glViewport->mapToRemote(localPoint);
    }
#endif
    // Fallback: linear scale mapping when GL viewport is not available
    if ( m_remoteSize.isEmpty() ) {
        return localPoint;
    }
    const QSize vs = viewportSize();
    if ( vs.isEmpty() ) {
        return localPoint;
    }
    const double sx = static_cast<double>(m_remoteSize.width()) / vs.width();
    const double sy = static_cast<double>(m_remoteSize.height()) / vs.height();
    return QPoint(
        static_cast<int>(localPoint.x() * sx),
        static_cast<int>(localPoint.y() * sy)
    );
}

QPoint RenderManager::mapFromRemote(const QPoint& remotePoint) const {
#ifndef QT_NO_OPENGL
    if ( m_glModeActive && m_glViewport ) {
        return m_glViewport->mapFromRemote(remotePoint);
    }
#endif
    // Fallback: linear scale mapping
    if ( m_remoteSize.isEmpty() ) {
        return remotePoint;
    }
    const QSize vs = viewportSize();
    if ( vs.isEmpty() ) {
        return remotePoint;
    }
    const double sx = static_cast<double>(vs.width()) / m_remoteSize.width();
    const double sy = static_cast<double>(vs.height()) / m_remoteSize.height();
    return QPoint(
        static_cast<int>(remotePoint.x() * sx),
        static_cast<int>(remotePoint.y() * sy)
    );
}

QRect RenderManager::mapToRemote(const QRect& localRect) const {
    QPoint topLeft = mapToRemote(localRect.topLeft());
    QPoint bottomRight = mapToRemote(localRect.bottomRight());
    return QRect(topLeft, bottomRight);
}

QRect RenderManager::mapFromRemote(const QRect& remoteRect) const {
    QPoint topLeft = mapFromRemote(remoteRect.topLeft());
    QPoint bottomRight = mapFromRemote(remoteRect.bottomRight());
    return QRect(topLeft, bottomRight);
}

void RenderManager::onViewResized() {
    calculateScaledSize();
}

void RenderManager::calculateScaledSize() {
    if ( m_remoteSize.isEmpty() ) {
        m_scaledSize = QSize(1024, 768);
        return;
    }

    const QSize vs = viewportSize();
    if ( vs.isEmpty() ) {
        m_scaledSize = m_remoteSize;
        return;
    }

    double scaleX = static_cast<double>(vs.width()) / m_remoteSize.width();
    double scaleY = static_cast<double>(vs.height()) / m_remoteSize.height();
    double scale = qMin(scaleX, scaleY);

    m_scaledSize = QSize(
        static_cast<int>(m_remoteSize.width() * scale),
        static_cast<int>(m_remoteSize.height() * scale)
    );
}
