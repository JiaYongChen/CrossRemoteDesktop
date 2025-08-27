#include "rendermanager.h"
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsPixmapItem>
#include <QtWidgets/QGraphicsView>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QtCore/QTimer>
#include <QtCore/QDebug>
#include <QtGui/QPainter>
#include <cmath>

RenderManager::RenderManager(QGraphicsView *graphicsView, QObject *parent)
    : QObject(parent)
    , m_graphicsView(graphicsView)
    , m_scene(nullptr)
    , m_pixmapItem(nullptr)
    , m_remoteSize(1024, 768)
    , m_scaledSize(1024, 768)
    , m_viewMode(FitToWindow)
    , m_scaleFactor(1.0)
    , m_customScaleFactor(1.0)
    , m_updateTimer(new QTimer(this))
    , m_pendingUpdate(false)
    , m_imageQuality(SmoothRendering)
    , m_animationMode(NoAnimation)
    , m_cacheEnabled(true)
    , m_cacheSizeLimit(100)
    , m_currentCacheSize(0)
{
    // 初始化更新定时器
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(16); // ~60 FPS
    connect(m_updateTimer, &QTimer::timeout, this, &RenderManager::updateDisplay);
}

RenderManager::~RenderManager()
{
    // 析构函数中清理资源
    if (m_scene) {
        delete m_scene;
    }
}

void RenderManager::initializeScene()
{
    if (!m_graphicsView) {
        qWarning() << "RenderManager: Graphics view is null";
        return;
    }
    
    // 创建图形场景
    if (!m_scene) {
        m_scene = new QGraphicsScene(this);
        m_graphicsView->setScene(m_scene);
        
        // 连接场景变化信号
        connect(m_scene, &QGraphicsScene::changed, this, &RenderManager::onSceneChanged);
    }
    
    // 确保像素图项存在
    ensurePixmapItem();
}

void RenderManager::setupView()
{
    if (!m_graphicsView) {
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

void RenderManager::setRemoteScreen(const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        qWarning() << "RenderManager: Received null pixmap";
        return;
    }
    
    m_remoteScreen = pixmap;
    m_remoteSize = pixmap.size();
    
    // 确保像素图项存在
    ensurePixmapItem();
    
    if (m_pixmapItem) {
        m_pixmapItem->setPixmap(pixmap);
    }
    
    // 更新场景矩形
    updateSceneRect();
    
    // 应用当前视图模式
    applyViewMode();
    
    // 计算缩放后的尺寸
    calculateScaledSize();
    
    // 更新显示
    forceUpdate();
}

void RenderManager::updateRemoteScreen(const QPixmap &screen)
{
    setRemoteScreen(screen);
}

void RenderManager::updateRemoteRegion(const QPixmap &region, const QRect &rect)
{
    if (region.isNull() || rect.isEmpty()) {
        qWarning() << "RenderManager: Invalid region update parameters";
        return;
    }
    
    if (m_remoteScreen.isNull()) {
        qWarning() << "RenderManager: No remote screen to update";
        return;
    }
    
    // 实现真正的区域更新
    QPixmap updatedScreen = m_remoteScreen.copy();
    QPainter painter(&updatedScreen);
    
    // 根据图片质量设置渲染提示
    switch (m_imageQuality) {
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
    painter.drawPixmap(rect, region);
    painter.end();
    
    // 更新远程屏幕
    m_remoteScreen = updatedScreen;
    
    // 确保像素图项存在并更新
    ensurePixmapItem();
    if (m_pixmapItem) {
        m_pixmapItem->setPixmap(m_remoteScreen);
    }
    
    // 只更新指定区域
    if (m_scene) {
        m_scene->update(rect);
    }
    
    // 延迟更新显示
    scheduleUpdate();
}

void RenderManager::setViewMode(ViewMode mode)
{
    if (m_viewMode != mode) {
        m_viewMode = mode;
        applyViewMode();
        emit viewModeChanged(mode);
    }
}

void RenderManager::applyViewMode()
{
    if (!m_graphicsView || !m_pixmapItem || m_remoteScreen.isNull()) {
        return;
    }
    
    switch (m_viewMode) {
        case FitToWindow: {
            // 计算缩放因子以适应整个图像到视图中
            QSize viewSize = m_graphicsView->viewport()->size();
            QSize imageSize = m_remoteSize;
            
            if (imageSize.isEmpty() || viewSize.isEmpty()) {
                return;
            }
            
            double scaleX = static_cast<double>(viewSize.width()) / imageSize.width();
            double scaleY = static_cast<double>(viewSize.height()) / imageSize.height();
            double scale = qMin(scaleX, scaleY);
            
            m_scaleFactor = scale;
            
            // 应用变换
            QTransform transform;
            transform.scale(scale, scale);
            m_graphicsView->setTransform(transform);
            
            // 居中显示
            m_graphicsView->centerOn(m_pixmapItem);
            break;
        }
        case ActualSize: {
            m_scaleFactor = 1.0;
            m_graphicsView->resetTransform();
            break;
        }
        case CustomScale: {
            QTransform transform;
            transform.scale(m_customScaleFactor, m_customScaleFactor);
            m_graphicsView->setTransform(transform);
            m_scaleFactor = m_customScaleFactor;
            break;
        }
        case FillWindow: {
            // 缩放以填充整个视图，可能会裁剪图像
            QSize viewSize = m_graphicsView->viewport()->size();
            QSize imageSize = m_remoteSize;
            
            if (imageSize.isEmpty() || viewSize.isEmpty()) {
                return;
            }
            
            double scaleX = static_cast<double>(viewSize.width()) / imageSize.width();
            double scaleY = static_cast<double>(viewSize.height()) / imageSize.height();
            double scale = qMax(scaleX, scaleY);
            
            m_scaleFactor = scale;
            
            QTransform transform;
            transform.scale(scale, scale);
            m_graphicsView->setTransform(transform);
            
            m_graphicsView->centerOn(m_pixmapItem);
            break;
        }
    }
    
    emit scaleFactorChanged(m_scaleFactor);
}

void RenderManager::setScaleFactor(double factor)
{
    if (factor <= 0.0) {
        qWarning() << "RenderManager: Invalid scale factor:" << factor;
        return;
    }
    
    m_customScaleFactor = factor;
    if (m_viewMode == CustomScale) {
        applyViewMode();
    }
}

void RenderManager::setCustomScaleFactor(double factor)
{
    setScaleFactor(factor);
}

QPoint RenderManager::mapToRemote(const QPoint &localPoint) const
{
    if (!m_graphicsView || !m_pixmapItem || m_remoteSize.isEmpty()) {
        return localPoint;
    }
    
    QPointF scenePoint = m_graphicsView->mapToScene(localPoint);
    QPointF itemPoint = m_pixmapItem->mapFromScene(scenePoint);
    return itemPoint.toPoint();
}

QPoint RenderManager::mapFromRemote(const QPoint &remotePoint) const
{
    if (!m_graphicsView || !m_pixmapItem || m_remoteSize.isEmpty()) {
        return remotePoint;
    }
    
    QPointF itemPoint = QPointF(remotePoint);
    QPointF scenePoint = m_pixmapItem->mapToScene(itemPoint);
    QPoint viewPoint = m_graphicsView->mapFromScene(scenePoint);
    return viewPoint;
}

QPixmap RenderManager::getRemoteScreen() const
{
    if (m_pixmapItem) {
        return m_pixmapItem->pixmap();
    }
    return QPixmap();
}

QRect RenderManager::mapToRemote(const QRect &localRect) const
{
    QPoint topLeft = mapToRemote(localRect.topLeft());
    QPoint bottomRight = mapToRemote(localRect.bottomRight());
    return QRect(topLeft, bottomRight);
}

QRect RenderManager::mapFromRemote(const QRect &remoteRect) const
{
    QPoint topLeft = mapFromRemote(remoteRect.topLeft());
    QPoint bottomRight = mapFromRemote(remoteRect.bottomRight());
    return QRect(topLeft, bottomRight);
}

void RenderManager::updateDisplay()
{
    if (m_pendingUpdate) {
        m_pendingUpdate = false;
        if (m_graphicsView) {
            m_graphicsView->update();
        }
    }
}

void RenderManager::forceUpdate()
{
    if (m_graphicsView) {
        m_graphicsView->update();
    }
}

void RenderManager::enableOpenGL(bool enable)
{
    if (!m_graphicsView) {
        return;
    }
    
    if (enable) {
        QOpenGLWidget *openGLWidget = new QOpenGLWidget();
        m_graphicsView->setViewport(openGLWidget);
        qDebug() << "RenderManager: OpenGL rendering enabled";
    } else {
        m_graphicsView->setViewport(new QWidget());
        qDebug() << "RenderManager: OpenGL rendering disabled";
    }
}

void RenderManager::setUpdateMode(QGraphicsView::ViewportUpdateMode mode)
{
    if (m_graphicsView) {
        m_graphicsView->setViewportUpdateMode(mode);
    }
}

void RenderManager::fitToWindow()
{
    setViewMode(FitToWindow);
}

void RenderManager::actualSize()
{
    setViewMode(ActualSize);
}

void RenderManager::zoomIn()
{
    double newScale = m_customScaleFactor * 1.25;
    if (newScale <= 10.0) { // 限制最大缩放
        setCustomScaleFactor(newScale);
        setViewMode(CustomScale);
    }
}

void RenderManager::zoomOut()
{
    double newScale = m_customScaleFactor / 1.25;
    if (newScale >= 0.1) { // 限制最小缩放
        setCustomScaleFactor(newScale);
        setViewMode(CustomScale);
    }
}

void RenderManager::resetZoom()
{
    setCustomScaleFactor(1.0);
    setViewMode(ActualSize);
}

void RenderManager::handleResize(const QSize &newSize)
{
    Q_UNUSED(newSize)
    
    // Update view transformation when window is resized
    if (m_viewMode == FitToWindow || m_viewMode == FillWindow) {
        applyViewMode();
    }
}

void RenderManager::onViewResized()
{
    if (m_viewMode == FitToWindow || m_viewMode == FillWindow) {
        applyViewMode();
    }
    calculateScaledSize();
}

void RenderManager::onSceneChanged()
{
    if (!m_updateTimer->isActive()) {
        m_pendingUpdate = true;
        m_updateTimer->start();
    }
}

void RenderManager::calculateScaledSize()
{
    if (m_remoteSize.isEmpty()) {
        m_scaledSize = QSize(1024, 768);
        return;
    }
    
    switch (m_viewMode) {
        case FitToWindow:
        case FillWindow: {
            if (!m_graphicsView) {
                m_scaledSize = m_remoteSize;
                return;
            }
            
            QSize viewSize = m_graphicsView->viewport()->size();
            if (viewSize.isEmpty()) {
                m_scaledSize = m_remoteSize;
                return;
            }
            
            double scaleX = static_cast<double>(viewSize.width()) / m_remoteSize.width();
            double scaleY = static_cast<double>(viewSize.height()) / m_remoteSize.height();
            
            double scale = (m_viewMode == FitToWindow) ? qMin(scaleX, scaleY) : qMax(scaleX, scaleY);
            
            m_scaledSize = QSize(
                static_cast<int>(m_remoteSize.width() * scale),
                static_cast<int>(m_remoteSize.height() * scale)
            );
            break;
        }
        case ActualSize:
            m_scaledSize = m_remoteSize;
            break;
        case CustomScale:
            m_scaledSize = QSize(
                static_cast<int>(m_remoteSize.width() * m_customScaleFactor),
                static_cast<int>(m_remoteSize.height() * m_customScaleFactor)
            );
            break;
    }
}

void RenderManager::updateSceneRect()
{
    if (m_scene && !m_remoteSize.isEmpty()) {
        m_scene->setSceneRect(0, 0, m_remoteSize.width(), m_remoteSize.height());
    }
}

void RenderManager::updateViewTransform()
{
    // 当前实现中，变换在applyViewMode中处理
    // 这个方法保留用于未来的扩展
}

void RenderManager::ensurePixmapItem()
{
    if (!m_scene) {
        qWarning() << "RenderManager: Scene is null, cannot create pixmap item";
        return;
    }
    
    if (!m_pixmapItem) {
        m_pixmapItem = m_scene->addPixmap(QPixmap());
        if (m_pixmapItem) {
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
void RenderManager::setImageQuality(ImageQuality quality)
{
    if (m_imageQuality != quality) {
        m_imageQuality = quality;
        applyImageQualitySettings();
        forceUpdate();
    }
}

/**
 * @brief 设置缩放动画模式
 */
void RenderManager::setAnimationMode(AnimationMode mode)
{
    m_animationMode = mode;
}

/**
 * @brief 启用或禁用图片缓存
 */
void RenderManager::enableImageCache(bool enable)
{
    m_cacheEnabled = enable;
    if (!enable) {
        clearImageCache();
    }
}

/**
 * @brief 清除图片缓存
 */
void RenderManager::clearImageCache()
{
    m_pixmapCache.clear();
    m_currentCacheSize = 0;
}

/**
 * @brief 设置缓存大小限制
 */
void RenderManager::setCacheSizeLimit(int sizeMB)
{
    m_cacheSizeLimit = sizeMB;
    // 如果当前缓存超过限制，清理缓存
    if (m_currentCacheSize > sizeMB * 1024 * 1024) {
        clearImageCache();
    }
}

/**
 * @brief 应用图片质量设置
 */
void RenderManager::applyImageQualitySettings()
{
    if (!m_graphicsView) {
        return;
    }
    
    switch (m_imageQuality) {
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
void RenderManager::scheduleUpdate()
{
    if (!m_pendingUpdate) {
        m_pendingUpdate = true;
        m_updateTimer->start();
    }
}