#include "clientremotewindow.h"
#include "tcpclient.h"
#include "./managers/sessionmanager.h"
#include "clientmanager.h"
#include "../common/core/uiconstants.h"
#include "../common/core/messageconstants.h"

#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsPixmapItem>
#include <QtWidgets/QGraphicsRectItem>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QtCore/QPropertyAnimation>
#include <QtCore/QEasingCurve>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QMimeData>
#include <QDrag>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QUrl>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QScrollBar>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QStatusBar>
#include <QToolBar>
#include <QActionGroup>
#include <QSettings>
#include <QDebug>
#include <QtWidgets/QMenuBar>
#include <QtGui/QKeySequence>
#include <QtGui/QIcon>
#include <cmath>

ClientRemoteWindow::ClientRemoteWindow(const QString &connectionId, QWidget *parent)
    : QGraphicsView(parent)
    , m_connectionId(connectionId)
    , m_scene(nullptr)
    , m_pixmapItem(nullptr)
    , m_cursorItem(nullptr)
    , m_contextMenu(nullptr)
    , m_fitToWindowAction(nullptr)
    , m_actualSizeAction(nullptr)
    , m_zoomInAction(nullptr)
    , m_zoomOutAction(nullptr)
    , m_fullScreenAction(nullptr)
    , m_screenshotAction(nullptr)
    , m_disconnectAction(nullptr)
    , m_reconnectAction(nullptr)
    , m_settingsAction(nullptr)
    , m_remoteSize(1024, 768)
    , m_scaledSize(1024, 768)
    , m_connectionState(Disconnected)
    , m_viewMode(FitToWindow)
    , m_scaleFactor(1.0)
    , m_customScaleFactor(1.0)
    , m_isFullScreen(false)
    , m_inputEnabled(true)
    , m_keyboardGrabbed(false)
    , m_mouseGrabbed(false)
    , m_lastMousePos(-1, -1)
    , m_clipboardSyncEnabled(true)
    , m_clipboard(QApplication::clipboard())
    , m_fileTransferEnabled(true)
    , m_frameRate(60)
    , m_compressionLevel(6)
    , m_statsTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_currentFPS(0.0)
    , m_cursorVisible(true)
    , m_cursorPosition(0, 0)
    , m_dragging(false)
    , m_lastPanPoint(0, 0)
    , m_updateTimer(new QTimer(this))
    , m_pendingUpdate(false)
    , m_showPerformanceInfo(false)
    , m_showCursor(true)
    , m_tcpClient(nullptr)
    , m_sessionManager(nullptr)
{
    setupScene();
    setupView();
    setupUI();
    setupActions();
    setupContextMenu();
    setupConnections();
    
    // Set window properties
    setWindowTitle(QString("远程桌面 - %1").arg(connectionId));
    setMinimumSize(400, 300);
    resize(1024, 768);
    
    // Enable drag and drop
    setAcceptDrops(true);
    
    // Set focus policy
    setFocusPolicy(Qt::StrongFocus);
    
    // Start performance monitoring
    m_statsTimer->start(1000); // Update every second
}

ClientRemoteWindow::~ClientRemoteWindow()
{
    if (m_tcpClient) {
        m_tcpClient->disconnectFromHost();
    }
}

void ClientRemoteWindow::setupScene()
{
    m_scene = new QGraphicsScene(this);
    m_scene->setBackgroundBrush(QBrush(Qt::black));
    
    // Create pixmap item for remote screen
    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);
    
    // Create cursor item
    m_cursorItem = new QGraphicsRectItem();
    m_cursorItem->setBrush(QBrush(Qt::white));
    m_cursorItem->setPen(QPen(Qt::black));
    m_cursorItem->setRect(0, 0, 2, 2);
    m_cursorItem->setVisible(false);
    m_scene->addItem(m_cursorItem);
    
    setScene(m_scene);
}

void ClientRemoteWindow::setupView()
{
    // Set view properties
    setDragMode(QGraphicsView::NoDrag);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setRenderHint(QPainter::Antialiasing, false);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    
    // Enable OpenGL if available
    enableOpenGL(true);
    
    // Set transformation anchor
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    
    // Set initial view mode
    setViewMode(FitToWindow);
}

void ClientRemoteWindow::setupUI()
{
    // UI setup will be handled by actions and context menu
}

void ClientRemoteWindow::setupActions()
{
    // Create actions for context menu and shortcuts
    m_fitToWindowAction = new QAction("Fit to Window", this);
    m_actualSizeAction = new QAction("Actual Size", this);
    m_zoomInAction = new QAction("Zoom In", this);
    m_zoomOutAction = new QAction("Zoom Out", this);
    m_fullScreenAction = new QAction("Full Screen", this);
    m_screenshotAction = new QAction("Take Screenshot", this);
    m_disconnectAction = new QAction("Disconnect", this);
    m_reconnectAction = new QAction("Reconnect", this);
    m_settingsAction = new QAction("Settings", this);
}

void ClientRemoteWindow::setupContextMenu()
{
    m_contextMenu = new QMenu(this);
    m_contextMenu->addAction(m_fitToWindowAction);
    m_contextMenu->addAction(m_actualSizeAction);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_zoomInAction);
    m_contextMenu->addAction(m_zoomOutAction);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_fullScreenAction);
    m_contextMenu->addAction(m_screenshotAction);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_reconnectAction);
    m_contextMenu->addAction(m_disconnectAction);
}

void ClientRemoteWindow::setupConnections()
{
    // Connect actions
    connect(m_fitToWindowAction, &QAction::triggered, this, &ClientRemoteWindow::fitToWindow);
    connect(m_actualSizeAction, &QAction::triggered, this, &ClientRemoteWindow::actualSize);
    connect(m_zoomInAction, &QAction::triggered, this, &ClientRemoteWindow::zoomIn);
    connect(m_zoomOutAction, &QAction::triggered, this, &ClientRemoteWindow::zoomOut);
    connect(m_fullScreenAction, &QAction::triggered, this, &ClientRemoteWindow::toggleFullScreen);
    connect(m_screenshotAction, &QAction::triggered, this, &ClientRemoteWindow::takeScreenshot);
    connect(m_disconnectAction, &QAction::triggered, this, &ClientRemoteWindow::disconnectRequested);
    connect(m_reconnectAction, &QAction::triggered, this, &ClientRemoteWindow::reconnectRequested);
    
    // Connect timers
    connect(m_statsTimer, &QTimer::timeout, this, &ClientRemoteWindow::updatePerformanceStats);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ClientRemoteWindow::onReconnectTimer);
    connect(m_updateTimer, &QTimer::timeout, this, &ClientRemoteWindow::updateDisplay);
    
    // Connect clipboard
    if (m_clipboard) {
        connect(m_clipboard, &QClipboard::dataChanged, this, &ClientRemoteWindow::onClipboardChanged);
    }
    
    // Connect scene
    connect(m_scene, &QGraphicsScene::changed, this, &ClientRemoteWindow::onSceneChanged);
}

QString ClientRemoteWindow::getConnectionId() const
{
    return m_connectionId;
}

// Connection state management
void ClientRemoteWindow::setConnectionState(ConnectionState state)
{
    if (m_connectionState != state) {
        m_connectionState = state;
        update();
    }
}

ClientRemoteWindow::ConnectionState ClientRemoteWindow::connectionState() const
{
    return m_connectionState;
}

// Screen display methods
void ClientRemoteWindow::setRemoteScreen(const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        return;
    }
    
    m_remoteScreen = pixmap;
    m_remoteSize = pixmap.size();
    
    if (m_pixmapItem) {
        m_pixmapItem->setPixmap(pixmap);
    }
    
    // Update scene rect
    updateSceneRect();
    
    // Apply current view mode
    applyViewMode();
    
    // Calculate scaled size
    calculateScaledSize();
    
    // Update display
    updateDisplay();
    
    // Update view transform
    updateViewTransform();
    
    // Force update
    update();
    
    // Emit signal
    emit viewTransformChanged();
}

void ClientRemoteWindow::updateRemoteScreen(const QPixmap &screen)
{
    setRemoteScreen(screen);
}

void ClientRemoteWindow::updateRemoteRegion(const QPixmap &region, const QRect &rect)
{
    if (region.isNull() || rect.isEmpty()) {
        return;
    }
    
    // Update the specific region of the remote screen
    // This would require more complex implementation for partial updates
    updateRemoteScreen(region);
}

// View mode and scaling
void ClientRemoteWindow::applyViewMode()
{
    if (!m_pixmapItem || m_remoteScreen.isNull()) {
        return;
    }
    
    switch (m_viewMode) {
        case FitToWindow: {
            // Calculate scale factor to fit the entire image in the view
            QSize viewSize = viewport()->size();
            QSize imageSize = m_remoteSize;
            
            if (imageSize.isEmpty() || viewSize.isEmpty()) {
                return;
            }
            
            double scaleX = static_cast<double>(viewSize.width()) / imageSize.width();
            double scaleY = static_cast<double>(viewSize.height()) / imageSize.height();
            double scale = qMin(scaleX, scaleY);
            
            m_scaleFactor = scale;
            
            // Apply transform
            QTransform transform;
            transform.scale(scale, scale);
            setTransform(transform);
            
            // Center the view
            centerOn(m_pixmapItem);
            break;
        }
        case ActualSize: {
            m_scaleFactor = 1.0;
            resetTransform();
            break;
        }
        case CustomScale: {
            QTransform transform;
            transform.scale(m_customScaleFactor, m_customScaleFactor);
            setTransform(transform);
            m_scaleFactor = m_customScaleFactor;
            break;
        }
        case FillWindow: {
            // Scale to fill the entire view, may crop the image
            QSize viewSize = viewport()->size();
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
            setTransform(transform);
            
            centerOn(m_pixmapItem);
            break;
        }
    }
    
    emit scaleFactorChanged(m_scaleFactor);
}

void ClientRemoteWindow::setViewMode(ViewMode mode)
{
    if (m_viewMode != mode) {
        m_viewMode = mode;
        applyViewMode();
        emit viewModeChanged(mode);
    }
}

ClientRemoteWindow::ViewMode ClientRemoteWindow::viewMode() const
{
    return m_viewMode;
}

void ClientRemoteWindow::updateSceneRect()
{
    if (m_scene && !m_remoteSize.isEmpty()) {
        m_scene->setSceneRect(0, 0, m_remoteSize.width(), m_remoteSize.height());
    }
}

// Scaling methods
void ClientRemoteWindow::setScaleFactor(double factor)
{
    if (factor > 0 && factor != m_scaleFactor) {
        m_customScaleFactor = factor;
        if (m_viewMode == CustomScale) {
            applyViewMode();
        }
    }
}

double ClientRemoteWindow::scaleFactor() const
{
    return m_scaleFactor;
}

// Full screen
void ClientRemoteWindow::setFullScreen(bool fullScreen)
{
    m_isFullScreen = fullScreen;
}

bool ClientRemoteWindow::isFullScreen() const
{
    return m_isFullScreen;
}

// Input control
void ClientRemoteWindow::setInputEnabled(bool enabled)
{
    m_inputEnabled = enabled;
}

bool ClientRemoteWindow::isInputEnabled() const
{
    return m_inputEnabled;
}

void ClientRemoteWindow::setKeyboardGrabbed(bool grabbed)
{
    m_keyboardGrabbed = grabbed;
}

bool ClientRemoteWindow::isKeyboardGrabbed() const
{
    return m_keyboardGrabbed;
}

void ClientRemoteWindow::setMouseGrabbed(bool grabbed)
{
    m_mouseGrabbed = grabbed;
}

bool ClientRemoteWindow::isMouseGrabbed() const
{
    return m_mouseGrabbed;
}

// Clipboard synchronization
void ClientRemoteWindow::setClipboardSyncEnabled(bool enabled)
{
    m_clipboardSyncEnabled = enabled;
}

bool ClientRemoteWindow::isClipboardSyncEnabled() const
{
    return m_clipboardSyncEnabled;
}

// File transfer
void ClientRemoteWindow::setFileTransferEnabled(bool enabled)
{
    m_fileTransferEnabled = enabled;
}

bool ClientRemoteWindow::isFileTransferEnabled() const
{
    return m_fileTransferEnabled;
}

// Performance settings
void ClientRemoteWindow::setFrameRate(int fps)
{
    m_frameRate = fps;
}

int ClientRemoteWindow::frameRate() const
{
    return m_frameRate;
}

// Compression
void ClientRemoteWindow::setCompressionLevel(int level)
{
    m_compressionLevel = level;
}

int ClientRemoteWindow::compressionLevel() const
{
    return m_compressionLevel;
}

// Performance monitoring
double ClientRemoteWindow::currentFPS() const
{
    return m_currentFPS;
}

// Session control
void ClientRemoteWindow::startSession()
{
    if (m_sessionManager) {
        // Start session logic
    }
}

void ClientRemoteWindow::pauseSession()
{
    if (m_sessionManager) {
        // Pause session logic
    }
}

void ClientRemoteWindow::resumeSession()
{
    if (m_sessionManager) {
        // Resume session logic
    }
}

void ClientRemoteWindow::terminateSession()
{
    if (m_sessionManager) {
        // 终止会话
        m_sessionManager->terminateSession();
    }
}

// Public slots
void ClientRemoteWindow::fitToWindow()
{
    setViewMode(FitToWindow);
}

void ClientRemoteWindow::actualSize()
{
    setViewMode(ActualSize);
}

void ClientRemoteWindow::zoomIn()
{
    if (m_viewMode != CustomScale) {
        m_customScaleFactor = m_scaleFactor;
        setViewMode(CustomScale);
    }
    m_customScaleFactor *= 1.25;
    setScaleFactor(m_customScaleFactor);
}

void ClientRemoteWindow::zoomOut()
{
    if (m_viewMode != CustomScale) {
        m_customScaleFactor = m_scaleFactor;
        setViewMode(CustomScale);
    }
    m_customScaleFactor /= 1.25;
    setScaleFactor(m_customScaleFactor);
}

void ClientRemoteWindow::resetZoom()
{
    m_customScaleFactor = 1.0;
    setViewMode(ActualSize);
}

void ClientRemoteWindow::toggleFullScreen()
{
    setFullScreen(!m_isFullScreen);
}

void ClientRemoteWindow::takeScreenshot()
{
    saveScreenshot();
}

void ClientRemoteWindow::showConnectionInfo()
{
    // Show connection information dialog
}

void ClientRemoteWindow::showPerformanceStats()
{
    m_showPerformanceInfo = !m_showPerformanceInfo;
    update();
}

// Event handlers
void ClientRemoteWindow::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);
    
    QPainter painter(viewport());
    
    // Draw connection state overlay
    drawConnectionState(painter);
    
    // Draw performance info if enabled
    if (m_showPerformanceInfo) {
        drawPerformanceInfo(painter);
    }
    
    // Draw cursor if enabled
    if (m_showCursor) {
        drawCursor(painter);
    }
}

void ClientRemoteWindow::mousePressEvent(QMouseEvent *event)
{
    if (m_inputEnabled) {
        handleMouseInput(event, true);
    }
    QGraphicsView::mousePressEvent(event);
}

void ClientRemoteWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_inputEnabled) {
        handleMouseInput(event, false);
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ClientRemoteWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_inputEnabled) {
        QPoint remotePos = mapToRemote(event->pos());
        emit mouseMoved(remotePos, event->buttons());
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ClientRemoteWindow::wheelEvent(QWheelEvent *event)
{
    if (m_inputEnabled) {
        QPoint remotePos = mapToRemote(event->position().toPoint());
        int delta = event->angleDelta().y();
        Qt::Orientation orientation = (event->angleDelta().x() != 0) ? Qt::Horizontal : Qt::Vertical;
        emit wheelScrolled(remotePos, delta, orientation);
    }
    QGraphicsView::wheelEvent(event);
}

void ClientRemoteWindow::keyPressEvent(QKeyEvent *event)
{
    if (m_inputEnabled) {
        handleKeyboardInput(event, true);
    }
    QGraphicsView::keyPressEvent(event);
}

void ClientRemoteWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (m_inputEnabled) {
        handleKeyboardInput(event, false);
    }
    QGraphicsView::keyReleaseEvent(event);
}

void ClientRemoteWindow::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    
    // Reapply view mode to adjust scaling
    if (m_viewMode == FitToWindow || m_viewMode == FillWindow) {
        applyViewMode();
    }
}

void ClientRemoteWindow::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_contextMenu) {
        m_contextMenu->exec(event->globalPos());
    }
}

void ClientRemoteWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_fileTransferEnabled && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ClientRemoteWindow::dropEvent(QDropEvent *event)
{
    if (m_fileTransferEnabled && event->mimeData()->hasUrls()) {
        QStringList files;
        foreach (const QUrl &url, event->mimeData()->urls()) {
            files << url.toLocalFile();
        }
        emit fileDropped(files, event->position().toPoint().x(), event->position().toPoint().y());
        event->acceptProposedAction();
    }
}

void ClientRemoteWindow::focusInEvent(QFocusEvent *event)
{
    QGraphicsView::focusInEvent(event);
}

void ClientRemoteWindow::focusOutEvent(QFocusEvent *event)
{
    QGraphicsView::focusOutEvent(event);
}

void ClientRemoteWindow::closeEvent(QCloseEvent *event)
{
    // Emit window closed signal
    emit windowClosed();
    
    // Stop timers
    if (m_statsTimer) {
        m_statsTimer->stop();
    }
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
    
    // Disconnect from server
    if (m_tcpClient) {
        m_tcpClient->disconnectFromHost();
    }
    
    // Terminate session
    terminateSession();
    
    // Accept the close event
    event->accept();
    
    // Call parent implementation
    QGraphicsView::closeEvent(event);
}

// Private slots
void ClientRemoteWindow::onConnectionClosed()
{
    setConnectionState(Disconnected);
    // Handle connection closed
}

void ClientRemoteWindow::onConnectionError(const QString &error)
{
    setConnectionState(Disconnected);
    
    // Show error message
    QMessageBox::critical(this, "Connection Error", error);
    
    // Attempt to reconnect
    emit reconnectRequested();
}

void ClientRemoteWindow::onClipboardChanged()
{
    if (m_clipboardSyncEnabled && m_clipboard) {
        QString text = m_clipboard->text();
        if (text != m_lastClipboardText) {
            m_lastClipboardText = text;
            emit clipboardChanged(text);
        }
    }
}

void ClientRemoteWindow::updatePerformanceStats()
{
    // Update frame rate calculation
    updateFrameRate();
    
    // Update other performance metrics
    // This would be implemented based on actual network statistics
    
    // Update display if performance info is shown
    if (m_showPerformanceInfo) {
        update();
    }
    
    // Emit performance stats updated signal
    emit viewTransformChanged();
}

void ClientRemoteWindow::onReconnectTimer()
{
    // Handle reconnection attempt
}

void ClientRemoteWindow::onScreenDataReceived(const QPixmap &pixmap)
{
    updateRemoteScreen(pixmap);
}

void ClientRemoteWindow::onSessionStateChanged()
{
    // Handle session state changes
    if (m_sessionManager) {
        // Update UI based on session state
    }
}

void ClientRemoteWindow::onScreenUpdated(const QPixmap &screen)
{
    updateRemoteScreen(screen);
}

void ClientRemoteWindow::onPerformanceStatsUpdated()
{
    // Handle performance statistics updates
    if (m_showPerformanceInfo) {
        update();
    }
}

void ClientRemoteWindow::onConnectionStateChanged(int state)
{
    ConnectionState newState = static_cast<ConnectionState>(state);
    setConnectionState(newState);
    
    switch (newState) {
        case Disconnected:
            // Handle disconnected state
            break;
        case Connecting:
            // Handle connecting state
            break;
        case Connected:
            // Handle connected state
            break;
        case Reconnecting:
            // Handle reconnecting state
            break;
    }
}

// Private methods
void ClientRemoteWindow::updateDisplay()
{
    if (m_pendingUpdate) {
        m_pendingUpdate = false;
        update();
    }
}

void ClientRemoteWindow::calculateScaledSize()
{
    if (m_remoteSize.isEmpty()) {
        m_scaledSize = QSize(1024, 768);
        return;
    }
    
    switch (m_viewMode) {
        case FitToWindow:
        case FillWindow: {
            QSize viewSize = viewport()->size();
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

QPoint ClientRemoteWindow::mapToRemote(const QPoint &localPoint) const
{
    if (m_pixmapItem && !m_remoteSize.isEmpty()) {
        QPointF scenePoint = mapToScene(localPoint);
        QPointF itemPoint = m_pixmapItem->mapFromScene(scenePoint);
        return itemPoint.toPoint();
    }
    return localPoint;
}

QPoint ClientRemoteWindow::mapFromRemote(const QPoint &remotePoint) const
{
    if (m_pixmapItem && !m_remoteSize.isEmpty()) {
        QPointF itemPoint = QPointF(remotePoint);
        QPointF scenePoint = m_pixmapItem->mapToScene(itemPoint);
        QPoint viewPoint = mapFromScene(scenePoint);
        return viewPoint;
    }
    return remotePoint;
}

void ClientRemoteWindow::drawConnectionState(QPainter &painter)
{
    QString stateText;
    QColor stateColor;
    
    switch (m_connectionState) {
        case Disconnected:
            stateText = "Disconnected";
            stateColor = Qt::red;
            break;
        case Connecting:
            stateText = "Connecting...";
            stateColor = Qt::yellow;
            break;
        case Connected:
            // Don't draw anything when connected
            return;
        case Reconnecting:
            stateText = "Reconnecting...";
            stateColor = QColor(255, 165, 0); // orange color
            break;
    }
    
    if (!stateText.isEmpty()) {
        painter.save();
        
        // Set up font and metrics
        QFont font = painter.font();
        font.setPointSize(16);
        font.setBold(true);
        painter.setFont(font);
        
        QFontMetrics metrics(font);
        QRect textRect = metrics.boundingRect(stateText);
        
        // Calculate position (center of view)
        QRect viewRect = viewport()->rect();
        int x = (viewRect.width() - textRect.width()) / 2;
        int y = (viewRect.height() - textRect.height()) / 2;
        
        // Draw background
        QRect backgroundRect = textRect.adjusted(-10, -5, 10, 5);
        backgroundRect.moveTopLeft(QPoint(x - 10, y - 5));
        painter.fillRect(backgroundRect, QColor(0, 0, 0, 128));
        
        // Draw text
        painter.setPen(stateColor);
        painter.drawText(x, y + metrics.ascent(), stateText);
        
        painter.restore();
    }
}

void ClientRemoteWindow::drawPerformanceInfo(QPainter &painter)
{
    painter.save();
    
    // Performance info text
    QStringList info;
    info << QString("FPS: %1").arg(m_currentFPS, 0, 'f', 1);
    info << QString("Scale: %1%").arg(m_scaleFactor * 100, 0, 'f', 0);
    
    QString infoText = info.join(" | ");
    
    // Draw performance info
    painter.setPen(Qt::white);
    painter.drawText(10, 20, infoText);
    
    painter.restore();
}

void ClientRemoteWindow::drawCursor(QPainter &painter)
{
    Q_UNUSED(painter)
    if (m_cursorVisible && m_cursorItem) {
        // Cursor drawing is handled by the graphics scene
    }
}

void ClientRemoteWindow::handleMouseInput(QMouseEvent *event, bool pressed)
{
    QPoint remotePos = mapToRemote(event->pos());
    emit mouseEvent(remotePos.x(), remotePos.y(), static_cast<int>(event->buttons()), pressed ? 1 : 0);
}

void ClientRemoteWindow::handleKeyboardInput(QKeyEvent *event, bool pressed)
{
    emit keyboardEvent(event->key(), static_cast<int>(event->modifiers()), pressed, event->text());
}

void ClientRemoteWindow::saveScreenshot(const QString &fileName)
{
    if (!m_remoteScreen.isNull()) {
        QString file = fileName.isEmpty() ? QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")) : fileName;
        m_remoteScreen.save(file);
    }
}

void ClientRemoteWindow::showContextMenu(const QPoint &position)
{
    if (m_contextMenu) {
        m_contextMenu->exec(mapToGlobal(position));
    }
}

void ClientRemoteWindow::setTcpClient(TcpClient *client)
{
    if (m_tcpClient) {
        // Disconnect old client signals
        disconnect(m_tcpClient, nullptr, this, nullptr);
    }
    
    m_tcpClient = client;
    
    if (m_tcpClient) {
        // Connect new client signals
        connect(m_tcpClient, &TcpClient::connected, this, [this]() {
            setConnectionState(Connected);
        });
        connect(m_tcpClient, &TcpClient::disconnected, this, &ClientRemoteWindow::onConnectionClosed);
        connect(m_tcpClient, &TcpClient::errorOccurred, this, [this](const QString &error) {
            onConnectionError(error);
        });
    }
}

void ClientRemoteWindow::onSceneChanged()
{
    if (!m_pendingUpdate) {
        m_pendingUpdate = true;
        m_updateTimer->start(16); // ~60 FPS
    }
}

void ClientRemoteWindow::updateViewTransform()
{
    // Update view transformation
    calculateScaledSize();
    emit viewTransformChanged();
}

void ClientRemoteWindow::updateCursorPosition(const QPoint &position)
{
    m_cursorPosition = position;
    if (m_cursorItem) {
        m_cursorItem->setPos(position);
    }
}

void ClientRemoteWindow::setCursorVisible(bool visible)
{
    m_cursorVisible = visible;
    if (m_cursorItem) {
        m_cursorItem->setVisible(visible);
    }
}

void ClientRemoteWindow::enableOpenGL(bool enable)
{
    if (enable) {
        QOpenGLWidget *glWidget = new QOpenGLWidget();
        setViewport(glWidget);
    } else {
        setViewport(new QWidget());
    }
}

void ClientRemoteWindow::updateFrameRate()
{
    QDateTime now = QDateTime::currentDateTime();
    m_frameTimes.append(now);
    
    // Remove old frame times (older than 1 second)
    while (!m_frameTimes.isEmpty() && m_frameTimes.first().msecsTo(now) > 1000) {
        m_frameTimes.removeFirst();
    }
    
    // Calculate FPS
    m_currentFPS = m_frameTimes.size();
}

void ClientRemoteWindow::setSessionManager(SessionManager *sessionManager)
{
    if (m_sessionManager) {
        // Disconnect old session manager signals
        disconnect(m_sessionManager, nullptr, this, nullptr);
    }
    
    m_sessionManager = sessionManager;
    
    if (m_sessionManager) {
        // Connect new session manager signals
        connect(m_sessionManager, &SessionManager::sessionStateChanged, this, &ClientRemoteWindow::onSessionStateChanged);
        connect(m_sessionManager, &SessionManager::screenUpdated, this, &ClientRemoteWindow::onScreenUpdated);
        connect(m_sessionManager, &SessionManager::performanceStatsUpdated, this, &ClientRemoteWindow::onPerformanceStatsUpdated);
    }
}