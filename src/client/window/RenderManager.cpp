#include "RenderManager.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsPixmapItem>
#include <QtWidgets/QGraphicsView>
#ifndef QT_NO_OPENGL
#include <QtOpenGLWidgets/QOpenGLWidget>
#endif
#include <QtCore/QTimer>
#include <QtCore/QDebug>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <cmath>

RenderManager::RenderManager(QGraphicsView* graphicsView, QObject* parent)
    : QObject(parent)
    , m_graphicsView(graphicsView)
    , m_scene(nullptr)
    , m_pixmapItem(nullptr)
    , m_remoteSize(1024, 768)
    , m_scaledSize(1024, 768)
    , m_scaleFactor(1.0)
    , m_customScaleFactor(1.0)
    , m_updateTimer(new QTimer(this))
    , m_pendingUpdate(false)
    , m_imageQuality(SmoothRendering)
    , m_cacheEnabled(true)
    , m_cacheSizeLimit(100)
    , m_currentCacheSize(0) {
    // 初始化更新定时器
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(16); // ~60 FPS
    connect(m_updateTimer, &QTimer::timeout, this, &RenderManager::updateDisplay);

    initializeScene();
    setupView();
}

RenderManager::~RenderManager() {
    // 析构函数中清理资源
    if ( m_scene ) {
        delete m_scene;
    }
}

void RenderManager::initializeScene() {
    if ( !m_graphicsView ) {
        qWarning() << "RenderManager: Graphics view is null";
        return;
    }

    // 创建图形场景
    if ( !m_scene ) {
        m_scene = new QGraphicsScene(this);
        m_graphicsView->setScene(m_scene);

        // 连接场景变化信号
        connect(m_scene, &QGraphicsScene::changed, this, &RenderManager::onSceneChanged);
    }

    // 确保像素图项存在
    ensurePixmapItem();
}

void RenderManager::setupView() {
    if ( !m_graphicsView ) {
        qWarning() << "RenderManager: Graphics view is null";
        return;
    }

    // 设置视图属性
    m_graphicsView->setDragMode(QGraphicsView::ScrollHandDrag);

    // 根据图片质量设置渲染提示
    applyImageQualitySettings();

    // 设置优化标志
    m_graphicsView->setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);
    m_graphicsView->setOptimizationFlag(QGraphicsView::DontSavePainterState, true);

    // 设置缓存模式
    m_graphicsView->setCacheMode(QGraphicsView::CacheBackground);

    // 设置更新模式
    setUpdateMode(QGraphicsView::MinimalViewportUpdate);
}

void RenderManager::setRemoteScreen(const QImage& image) {
    if ( image.isNull() ) {
        qWarning() << "RenderManager: Received null image";
        return;
    }

    // 在主线程中将 QImage 转换为 QPixmap
    // QPixmap 应该只在主线程中创建和使用
    QPixmap pixmap = QPixmap::fromImage(image);

    m_remoteScreen = pixmap;
    m_remoteSize = image.size();

    // 确保像素图项存在
    ensurePixmapItem();

    if ( m_pixmapItem ) {
        m_pixmapItem->setPixmap(pixmap);
    }

    // 更新场景矩形
    updateSceneRect();

    // 计算缩放后的尺寸
    calculateScaledSize();

    // 让视图适应场景，完全显示远程屏幕
    if ( m_graphicsView && m_pixmapItem ) {
        m_graphicsView->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    }

    // 更新显示
    forceUpdate();
}

void RenderManager::updateRemoteScreen(const QImage& screen) {
    setRemoteScreen(screen);
}

void RenderManager::updateRemoteRegion(const QImage& region, const QRect& rect) {
    if ( region.isNull() || rect.isEmpty() ) {
        qWarning() << "RenderManager: Invalid region update parameters";
        return;
    }

    if ( m_remoteScreen.isNull() ) {
        qWarning() << "RenderManager: No remote screen to update";
        return;
    }

    // 在主线程中将 QImage 转换为 QPixmap
    QPixmap regionPixmap = QPixmap::fromImage(region);

    // 实现真正的区域更新
    QPixmap updatedScreen = m_remoteScreen.copy();
    QPainter painter(&updatedScreen);

    // 根据图片质量设置渲染提示
    switch ( m_imageQuality ) {
        case FastRendering:
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            break;
        case SmoothRendering:
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            break;
        case HighQualityRendering:
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.setRenderHint(QPainter::Antialiasing, true);
            break;
    }

    // 在指定区域绘制新内容
    painter.drawPixmap(rect, regionPixmap);
    painter.end();

    // 更新远程屏幕
    m_remoteScreen = updatedScreen;

    // 确保像素图项存在并更新
    ensurePixmapItem();
    if ( m_pixmapItem ) {
        m_pixmapItem->setPixmap(m_remoteScreen);
    }

    // 只更新指定区域
    if ( m_scene ) {
        m_scene->update(rect);
    }

    // 延迟更新显示
    scheduleUpdate();
}

void RenderManager::setScaleFactor(double factor) {
    if ( factor <= 0.0 ) {
        qWarning() << "RenderManager: Invalid scale factor:" << factor;
        return;
    }

    m_customScaleFactor = factor;
}

QPoint RenderManager::mapToRemote(const QPoint& localPoint) const {
    if ( !m_graphicsView || !m_pixmapItem || m_remoteSize.isEmpty() ) {
        return localPoint;
    }

    QPointF scenePoint = m_graphicsView->mapToScene(localPoint);
    QPointF itemPoint = m_pixmapItem->mapFromScene(scenePoint);
    return itemPoint.toPoint();
}

QPoint RenderManager::mapFromRemote(const QPoint& remotePoint) const {
    if ( !m_graphicsView || !m_pixmapItem || m_remoteSize.isEmpty() ) {
        return remotePoint;
    }

    QPointF itemPoint = QPointF(remotePoint);
    QPointF scenePoint = m_pixmapItem->mapToScene(itemPoint);
    QPoint viewPoint = m_graphicsView->mapFromScene(scenePoint);
    return viewPoint;
}

QPixmap RenderManager::getRemoteScreen() const {
    if ( m_pixmapItem ) {
        return m_pixmapItem->pixmap();
    }
    return QPixmap();
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

void RenderManager::updateDisplay() {
    if ( m_pendingUpdate ) {
        m_pendingUpdate = false;
        if ( m_graphicsView ) {
            m_graphicsView->update();
        }
    }
}

void RenderManager::forceUpdate() {
    if ( m_graphicsView ) {
        m_graphicsView->update();
    }
}

void RenderManager::enableOpenGL(bool enable) {
    if ( !m_graphicsView ) {
        return;
    }

#ifndef QT_NO_OPENGL
    if ( enable ) {
        QOpenGLWidget* openGLWidget = new QOpenGLWidget();
        m_graphicsView->setViewport(openGLWidget);
        qDebug() << "RenderManager: OpenGL rendering enabled";
    } else {
        m_graphicsView->setViewport(new QWidget());
        qDebug() << "RenderManager: OpenGL rendering disabled";
    }
#else
    // OpenGL is disabled, always use software rendering
    m_graphicsView->setViewport(new QWidget());
    qDebug() << "RenderManager: OpenGL disabled at compile time, using software rendering";
    Q_UNUSED(enable)
    #endif
}

void RenderManager::setUpdateMode(QGraphicsView::ViewportUpdateMode mode) {
    if ( m_graphicsView ) {
        m_graphicsView->setViewportUpdateMode(mode);
    }
}

void RenderManager::onViewResized() {
    calculateScaledSize();

    // 当视图大小改变时，重新适应场景以确保完全显示
    if ( m_graphicsView && m_pixmapItem && !m_remoteScreen.isNull() ) {
        m_graphicsView->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    }
}

void RenderManager::onSceneChanged() {
    if ( !m_updateTimer->isActive() ) {
        m_pendingUpdate = true;
        m_updateTimer->start();
    }
}

void RenderManager::calculateScaledSize() {
    if ( m_remoteSize.isEmpty() ) {
        m_scaledSize = QSize(1024, 768);
        return;
    }

    if ( !m_graphicsView ) {
        m_scaledSize = m_remoteSize;
        return;
    }

    QSize viewSize = m_graphicsView->viewport()->size();
    if ( viewSize.isEmpty() ) {
        m_scaledSize = m_remoteSize;
        return;
    }

    // 在AutoFit模式下，使用qMin确保图片完全显示
    double scaleX = static_cast<double>(viewSize.width()) / m_remoteSize.width();
    double scaleY = static_cast<double>(viewSize.height()) / m_remoteSize.height();
    double scale = qMin(scaleX, scaleY);

    m_scaledSize = QSize(
        static_cast<int>(m_remoteSize.width() * scale),
        static_cast<int>(m_remoteSize.height() * scale)
    );
}

void RenderManager::updateSceneRect() {
    if ( m_scene && !m_remoteSize.isEmpty() ) {
        m_scene->setSceneRect(0, 0, m_remoteSize.width(), m_remoteSize.height());
    }
}

void RenderManager::updateViewTransform() {
    // 这个方法保留用于未来的扩展
}

void RenderManager::ensurePixmapItem() {
    if ( !m_scene ) {
        qWarning() << "RenderManager: Scene is null, cannot create pixmap item";
        return;
    }

    if ( !m_pixmapItem ) {
        m_pixmapItem = m_scene->addPixmap(QPixmap());
        if ( m_pixmapItem ) {
            m_pixmapItem->setPos(0, 0);
            qDebug() << "RenderManager: Pixmap item created";
        } else {
            qWarning() << "RenderManager: Failed to create pixmap item";
        }
    }
}

/**
 * @brief 设置图片质量
 */
void RenderManager::setImageQuality(ImageQuality quality) {
    if ( m_imageQuality != quality ) {
        m_imageQuality = quality;
        applyImageQualitySettings();
        forceUpdate();
    }
}

/**
 * @brief 启用或禁用图片缓存
 */
void RenderManager::enableImageCache(bool enable) {
    m_cacheEnabled = enable;
    if ( !enable ) {
        clearImageCache();
    }
}

/**
 * @brief 清除图片缓存
 */
void RenderManager::clearImageCache() {
    m_pixmapCache.clear();
    m_currentCacheSize = 0;
}

/**
 * @brief 设置缓存大小限制
 */
void RenderManager::setCacheSizeLimit(int sizeMB) {
    m_cacheSizeLimit = sizeMB;
    // 如果当前缓存超过限制，清理缓存
    if ( m_currentCacheSize > sizeMB * 1024 * 1024 ) {
        clearImageCache();
    }
}

/**
 * @brief 应用图片质量设置
 */
void RenderManager::applyImageQualitySettings() {
    if ( !m_graphicsView ) {
        return;
    }

    switch ( m_imageQuality ) {
        case FastRendering:
            m_graphicsView->setRenderHint(QPainter::Antialiasing, false);
            m_graphicsView->setRenderHint(QPainter::SmoothPixmapTransform, false);
            break;
        case SmoothRendering:
            m_graphicsView->setRenderHint(QPainter::Antialiasing, true);
            m_graphicsView->setRenderHint(QPainter::SmoothPixmapTransform, true);
            break;
        case HighQualityRendering:
            m_graphicsView->setRenderHint(QPainter::Antialiasing, true);
            m_graphicsView->setRenderHint(QPainter::SmoothPixmapTransform, true);
            m_graphicsView->setRenderHint(QPainter::TextAntialiasing, true);
            break;
    }
}

/**
 * @brief 延迟更新调度
 */
void RenderManager::scheduleUpdate() {
    if ( !m_pendingUpdate ) {
        m_pendingUpdate = true;
        m_updateTimer->start();
    }
}