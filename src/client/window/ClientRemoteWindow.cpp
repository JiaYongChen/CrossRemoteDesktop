#include "ClientRemoteWindow.h"
#include "../network/TcpClient.h"
#include "../managers/SessionManager.h"
#include "../managers/ClipboardManager.h"
#include "../managers/FileTransferManager.h"
#include "../managers/CursorManager.h"
#include "../managers/InputHandler.h"
#include "../../common/core/config/UiConstants.h"
#include "../../common/core/config/MessageConstants.h"
#include "RenderManager.h"

#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsPixmapItem>
#include <QtWidgets/QGraphicsRectItem>
#ifndef QT_NO_OPENGL
#include <QtOpenGLWidgets/QOpenGLWidget>
#endif
#include <QtCore/QPropertyAnimation>
#include <QtCore/QEasingCurve>
#include <QtGui/QPainter>
#include <QtGui/QMouseEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QResizeEvent>
#include <QtGui/QPaintEvent>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QApplication>
#include <QtGui/QScreen>
#include <QtCore/QTimer>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtGui/QActionGroup>
#include <QtCore/QSettings>
#include <QtCore/QDebug>
#include <QtCore/QLoggingCategory>
#include <QtWidgets/QMenuBar>
#include <QtGui/QKeySequence>
#include <QtGui/QIcon>
#include <cmath>

Q_LOGGING_CATEGORY(lcClientRemoteWindow, "client.remote.window")

ClientRemoteWindow::ClientRemoteWindow(SessionManager* sessionManager, QWidget* parent)
    : QGraphicsView(parent)
    , m_connectionId(QString::number(0))
    , m_sessionManager(sessionManager)
    , m_connectionState(ConnectionManager::Disconnected)
    , m_isFullScreen(false)
    , m_isClosing(false) // 初始化关闭标志，默认不在关闭流程中
    , m_inputEnabled(true)
    , m_keyboardGrabbed(false)
    , m_mouseGrabbed(false)
    , m_lastMousePos(-1, -1)
    , m_clipboardManager(nullptr)
    , m_fileTransferManager(nullptr)
    , m_inputHandler(nullptr)
    , m_cursorManager(nullptr)
    , m_renderManager(nullptr)
    , m_lastPanPoint(0, 0)
    , m_showPerformanceInfo(false) {
    // 确保窗口关闭时自动删除，避免无父对象窗口内存泄漏
    // 使用属性而非手动deleteLater，减少重复释放风险
    setAttribute(Qt::WA_DeleteOnClose, true);
    qDebug() << "[ClientRemoteWindow] Constructor started for sessionManager:" << sessionManager;

    m_connectionId = sessionManager ? sessionManager->connectionId() : QString::number(0);

    // Initialize all managers using composition pattern
    initializeManagers();

    // Configure window properties (size, title, etc.)
    configureWindow();

    // Setup UI components
    setupScene();
    setupView();
    setWindowTitle(tr("%1").arg(sessionManager->currentHost()));

    // Setup connections to SessionManager if provided
    if ( m_sessionManager ) {
        setupManagerConnections();
    }
}

ClientRemoteWindow::~ClientRemoteWindow() {
    // 注意：不在析构中发射 windowClosed()，避免与 closeEvent 重复
    // Qt 父子关系将自动清理子对象
}

QString ClientRemoteWindow::connectionId() const {
    return m_connectionId;
}

// Window title management
void ClientRemoteWindow::updateWindowTitle(const QString& title) {
    if ( !title.isEmpty() ) {
        setWindowTitle(title);
    }
}

void ClientRemoteWindow::updateWindowTitle() {
    if ( m_sessionManager ) {
        QString host = m_sessionManager->currentHost();
        if ( !host.isEmpty() ) {
            // 根据连接状态显示不同的标题格式
            QString title;
            switch ( m_connectionState ) {
                case ConnectionManager::Connecting:
                    title = tr("%1 - %2").arg(host).arg(MessageConstants::UI::STATUS_CONNECTING);
                    break;
                case ConnectionManager::Connected:
                    title = tr("%1 - %2").arg(host).arg(MessageConstants::UI::STATUS_CONNECTED);
                    break;
                case ConnectionManager::Authenticating:
                    title = tr("%1 - %2").arg(host).arg(MessageConstants::UI::STATUS_AUTHENTICATING);
                    break;
                case ConnectionManager::Authenticated:
                    title = tr("%1 - %2").arg(host).arg(MessageConstants::UI::STATUS_AUTHENTICATED);
                    break;
                case ConnectionManager::Disconnecting:
                    title = tr("%1 - %2").arg(host).arg(MessageConstants::UI::STATUS_DISCONNECTING);
                    break;
                case ConnectionManager::Disconnected:
                    title = tr("%1 - %2").arg(host).arg(MessageConstants::UI::STATUS_DISCONNECTED);
                    break;
                case ConnectionManager::Reconnecting:
                    title = tr("%1 - %2").arg(host).arg(MessageConstants::UI::STATUS_RECONNECTING);
                    break;
                case ConnectionManager::Error:
                    title = tr("%1 - %2").arg(host).arg(MessageConstants::UI::STATUS_ERROR);
                    break;
                default:
                    title = host;
                    break;
            }
            setWindowTitle(title);
        }
    }
}

void ClientRemoteWindow::initializeManagers() {
    // Initialize managers using composition pattern
    // Each manager is responsible for a specific aspect of functionality

    // Clipboard management
    m_clipboardManager = new ClipboardManager(this);

    // File transfer management
    m_fileTransferManager = new FileTransferManager(this, this);

    // Input handling
    m_inputHandler = new InputHandler(this);

    // Cursor management (will be initialized when needed)
    m_cursorManager = nullptr;

    // Render and view management
    m_renderManager = new RenderManager(this, this);
}

void ClientRemoteWindow::configureWindow() {
    // 设置窗口的最小尺寸与初始尺寸，保证初次打开时有合理的显示区域
    // 最小尺寸设为400x225，确保基本可用性
    setMinimumSize(400, 225);
    // 默认初始尺寸设为1600x900，提供良好的初始体验
    resize(1600, 900);

    // 设置焦点策略，确保输入事件能够被正确接收和处理
    setFocusPolicy(Qt::StrongFocus);
}

void ClientRemoteWindow::enableManagerFeatures() {
    // Enable drag and drop through FileTransferManager
    if ( m_fileTransferManager ) {
        m_fileTransferManager->setEnabled(true);
    }
}

void ClientRemoteWindow::setupScene() {
    // Initialize render manager scene
    if ( m_renderManager ) {
        m_renderManager->initializeScene();
    }

    // Initialize cursor manager after scene is created
    if ( m_renderManager && m_renderManager->scene() ) {
        m_cursorManager = new CursorManager(m_renderManager->scene(), this);
    }
}

void ClientRemoteWindow::setupView() {
    // Delegate view setup to render manager
    if ( m_renderManager ) {
        m_renderManager->setupView();
    }

    // Set transformation anchor
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);

    // Enable mouse tracking
    setMouseTracking(true);
}

void ClientRemoteWindow::setupManagerConnections() {
    // Connect SessionManager signals
    if ( m_sessionManager ) {
        connect(m_sessionManager, &SessionManager::performanceStatsUpdated,
            this, &ClientRemoteWindow::onPerformanceStatsUpdated);
        connect(m_sessionManager, &SessionManager::screenUpdated,
            this, &ClientRemoteWindow::onScreenUpdated);

        // 连接状态变化信号，同步更新 UI 显示
        connect(m_sessionManager, &SessionManager::connectionStateChanged,
            this, &ClientRemoteWindow::setConnectionState);
    }

    // Connect clipboard manager signals
    if ( m_clipboardManager ) {
        // Forward clipboard changes to external listeners if needed
    }

    // Connect file transfer manager signals  
    if ( m_fileTransferManager ) {
        // Forward file drop events to external listeners if needed
    }

    // Connect input handler signals
    if ( m_inputHandler ) {
        // Connect input events to session manager for network transmission
        connect(m_inputHandler, &InputHandler::inputEventReady, this, [this](const InputEvent& event) {
            if ( m_sessionManager ) {
                switch ( event.type ) {
                    case InputEventType::MouseMove:
                    case InputEventType::MousePress:
                    case InputEventType::MouseRelease:
                        m_sessionManager->sendMouseEvent(event.position.x(), event.position.y(),
                            event.button, event.type == InputEventType::MousePress ? 1 : 0);
                        break;
                    case InputEventType::MouseWheel:
                        m_sessionManager->sendWheelEvent(event.position.x(), event.position.y(),
                            event.wheelDelta, Qt::Vertical);
                        break;
                    case InputEventType::KeyPress:
                    case InputEventType::KeyRelease:
                        m_sessionManager->sendKeyboardEvent(event.key, event.modifiers,
                            event.type == InputEventType::KeyPress, event.text);
                        break;
                }
            }
        });

        // Set screen size and scale factor for coordinate transformation
        if ( m_renderManager ) {
            m_inputHandler->setScreenSize(m_renderManager->remoteSize());
            m_inputHandler->setScaleFactor(m_renderManager->scaleFactor());
        }
    }

    // Connect render manager signals
    if ( m_renderManager ) {
        connect(m_renderManager, &RenderManager::scaleFactorChanged,
            this, &ClientRemoteWindow::scaleFactorChanged);

        // 连接窗口大小调整请求信号
        connect(m_renderManager, &RenderManager::windowResizeRequested,
            this, &ClientRemoteWindow::onWindowResizeRequested);
    }
}

// Connection state management
void ClientRemoteWindow::setConnectionState(ConnectionManager::ConnectionState state) {
    if ( m_connectionState != state ) {
        m_connectionState = state;
        // 状态变化时自动更新窗口标题
        updateWindowTitle();
    }
}

ConnectionManager::ConnectionState ClientRemoteWindow::connectionState() const {
    return m_connectionState;
}

// Screen display methods
void ClientRemoteWindow::setRemoteScreen(const QImage& image) {
    if ( m_renderManager ) {
        m_renderManager->setRemoteScreen(image);
    }
}

void ClientRemoteWindow::updateRemoteScreen(const QImage& screen) {
    if ( m_renderManager ) {
        m_renderManager->updateRemoteScreen(screen);
    }
}

void ClientRemoteWindow::updateRemoteRegion(const QImage& region, const QRect& rect) {
    if ( m_renderManager ) {
        m_renderManager->updateRemoteRegion(region, rect);
    }
}

// Scaling methods
void ClientRemoteWindow::setScaleFactor(double factor) {
    if ( m_renderManager ) {
        m_renderManager->setScaleFactor(factor);

    }
}

double ClientRemoteWindow::scaleFactor() const {
    if ( m_renderManager ) {
        return m_renderManager->scaleFactor();
    }
    return 1.0;
}

// Full screen
void ClientRemoteWindow::setFullScreen(bool fullScreen) {
    m_isFullScreen = fullScreen;
}

bool ClientRemoteWindow::isFullScreen() const {
    return m_isFullScreen;
}

/**
 * @brief 设置图片质量
 */
void ClientRemoteWindow::setImageQuality(RenderManager::ImageQuality quality) {
    if ( m_renderManager ) {
        m_renderManager->setImageQuality(quality);
    }
}

/**
 * @brief 获取当前图片质量
 */
RenderManager::ImageQuality ClientRemoteWindow::imageQuality() const {
    if ( m_renderManager ) {
        return m_renderManager->imageQuality();
    }
    return RenderManager::SmoothRendering;
}

/**
 * @brief 启用或禁用图片缓存
 */
void ClientRemoteWindow::enableImageCache(bool enable) {
    if ( m_renderManager ) {
        m_renderManager->enableImageCache(enable);
    }
}

/**
 * @brief 清除图片缓存
 */
void ClientRemoteWindow::clearImageCache() {
    if ( m_renderManager ) {
        m_renderManager->clearImageCache();
    }
}

/**
 * @brief 设置缓存大小限制
 */
void ClientRemoteWindow::setCacheSizeLimit(int sizeMB) {
    if ( m_renderManager ) {
        m_renderManager->setCacheSizeLimit(sizeMB);
    }
}

// Input control
void ClientRemoteWindow::setInputEnabled(bool enabled) {
    m_inputEnabled = enabled;
}

bool ClientRemoteWindow::isInputEnabled() const {
    return m_inputEnabled;
}

void ClientRemoteWindow::setKeyboardGrabbed(bool grabbed) {
    m_keyboardGrabbed = grabbed;
}

bool ClientRemoteWindow::isKeyboardGrabbed() const {
    return m_keyboardGrabbed;
}

void ClientRemoteWindow::setMouseGrabbed(bool grabbed) {
    m_mouseGrabbed = grabbed;
}

bool ClientRemoteWindow::isMouseGrabbed() const {
    return m_mouseGrabbed;
}

// Manager access methods
ClipboardManager* ClientRemoteWindow::clipboardManager() const {
    return m_clipboardManager;
}

FileTransferManager* ClientRemoteWindow::fileTransferManager() const {
    return m_fileTransferManager;
}

InputHandler* ClientRemoteWindow::inputHandler() const {
    return m_inputHandler;
}

CursorManager* ClientRemoteWindow::cursorManager() const {
    return m_cursorManager;
}

RenderManager* ClientRemoteWindow::renderManager() const {
    return m_renderManager;
}

// Performance settings
void ClientRemoteWindow::setFrameRate(int fps) {
    if ( m_sessionManager ) {
        m_sessionManager->setFrameRate(fps);
    }
}

int ClientRemoteWindow::frameRate() const {
    return m_sessionManager ? m_sessionManager->frameRate() : 30;
}

// Performance monitoring
double ClientRemoteWindow::currentFPS() const {
    // 若存在 SessionManager，则直接返回其统计的 FPS，否则返回 0
    return m_sessionManager ? m_sessionManager->performanceStats().currentFPS : 0.0;
}

// Public slots
void ClientRemoteWindow::toggleFullScreen() {
    setFullScreen(!m_isFullScreen);
}

void ClientRemoteWindow::takeScreenshot() {
    saveScreenshot();
}

void ClientRemoteWindow::showConnectionInfo() {
    // Show connection information dialog
}

void ClientRemoteWindow::showPerformanceStats() {
    m_showPerformanceInfo = !m_showPerformanceInfo;
    update();
}

// Event handlers
void ClientRemoteWindow::paintEvent(QPaintEvent* event) {
    QGraphicsView::paintEvent(event);

    QPainter painter(viewport());

    // Draw performance info if enabled
    if ( m_showPerformanceInfo ) {
        drawPerformanceInfo(painter);
    }

    // Draw cursor if enabled (delegated to CursorManager)
    if ( m_cursorManager && m_cursorManager->showCursor() ) {
        m_cursorManager->drawCursor(painter);
    }
}

void ClientRemoteWindow::mousePressEvent(QMouseEvent* event) {
    if ( m_inputEnabled && m_inputHandler ) {
        QPoint remotePos = mapToRemote(event->pos());
        m_inputHandler->handleMousePress(remotePos, event->button());
    }
    QGraphicsView::mousePressEvent(event);
}

void ClientRemoteWindow::mouseReleaseEvent(QMouseEvent* event) {
    if ( m_inputEnabled && m_inputHandler ) {
        QPoint remotePos = mapToRemote(event->pos());
        m_inputHandler->handleMouseRelease(remotePos, event->button());
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ClientRemoteWindow::mouseMoveEvent(QMouseEvent* event) {
    if ( m_inputEnabled && m_inputHandler ) {
        QPoint remotePos = mapToRemote(event->pos());
        m_inputHandler->handleMouseMove(remotePos);
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ClientRemoteWindow::wheelEvent(QWheelEvent* event) {
    if ( m_inputEnabled && m_inputHandler ) {
        QPoint remotePos = mapToRemote(event->position().toPoint());
        int delta = event->angleDelta().y();
        m_inputHandler->handleMouseWheel(remotePos, delta);
    }
    QGraphicsView::wheelEvent(event);
}

void ClientRemoteWindow::keyPressEvent(QKeyEvent* event) {
    if ( m_inputEnabled && m_inputHandler ) {
        m_inputHandler->handleKeyPress(event->key(), event->modifiers(), event->text());
    }
    QGraphicsView::keyPressEvent(event);
}

void ClientRemoteWindow::keyReleaseEvent(QKeyEvent* event) {
    if ( m_inputEnabled && m_inputHandler ) {
        m_inputHandler->handleKeyRelease(event->key(), event->modifiers());
    }
    QGraphicsView::keyReleaseEvent(event);
}

void ClientRemoteWindow::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);

    // 通知 RenderManager 视图大小已改变，以重新适应显示
    if ( m_renderManager ) {
        m_renderManager->onViewResized();
    }
}

// Note: Drag and drop events are now handled by FileTransferManager
// The events are automatically forwarded to the manager through Qt's event system

void ClientRemoteWindow::focusInEvent(QFocusEvent* event) {
    QGraphicsView::focusInEvent(event);
}

void ClientRemoteWindow::focusOutEvent(QFocusEvent* event) {
    QGraphicsView::focusOutEvent(event);
}

void ClientRemoteWindow::closeEvent(QCloseEvent* event) {
    qCDebug(lcClientRemoteWindow) << "closeEvent received, isClosing=" << m_isClosing;

    // 若已处于关闭流程，直接接受事件并返回，避免重入
    if ( m_isClosing ) {
        event->accept();
        return;
    }

    // 设置关闭标志，表示进入窗口关闭流程
    m_isClosing = true;

    // 先发射一次 windowClosed，保证上层（ClientManager）进行清理。
    // 通过标志位确保只发射一次，避免析构或其他路径再次发射。
    emit windowClosed();

    // 接受事件并调用父类实现，确保正常的 QWidget/QGraphicsView 关闭流程
    event->accept();
    QGraphicsView::closeEvent(event);
}

// Private slots
void ClientRemoteWindow::onConnectionClosed() {
    /*
     * 连接关闭回调（来自底层/会话）
     * - 不在窗口侧主动修改连接状态，交由 ConnectionManager 广播最终状态
     * - 此处可按需加入资源清理或提示逻辑
     */
     // Handle connection closed (no state changes here)
}

void ClientRemoteWindow::onConnectionError(const QString& error) {
    /*
     * 连接错误回调
     * - 不在窗口侧主动修改连接状态，交由 ConnectionManager 广播 Error/Disconnected 等状态
     * - 仅负责展示错误信息
     */
    QMessageBox::critical(this, "Connection Error", error);
}

// Note: Clipboard changes are now handled by ClipboardManager

void ClientRemoteWindow::onScreenUpdated(const QImage& screen) {
    updateRemoteScreen(screen);
}

void ClientRemoteWindow::onPerformanceStatsUpdated() {
    // Handle performance statistics updates
    if ( m_showPerformanceInfo ) {
        update();
    }
}

// Private methods
// updateDisplay and calculateScaledSize are now handled by RenderManager

QPoint ClientRemoteWindow::mapToRemote(const QPoint& localPoint) const {
    if ( m_renderManager ) {
        return m_renderManager->mapToRemote(localPoint);
    }
    return localPoint;
}

QPoint ClientRemoteWindow::mapFromRemote(const QPoint& remotePoint) const {
    if ( m_renderManager ) {
        return m_renderManager->mapFromRemote(remotePoint);
    }
    return remotePoint;
}

void ClientRemoteWindow::drawPerformanceInfo(QPainter& painter) {
    painter.save();

    // Get performance info from SessionManager
    QString sessionInfo = m_sessionManager ? m_sessionManager->getFormattedPerformanceInfo() : "No Session";

    // Add render info
    QStringList info;
    info << sessionInfo;

    double currentScale = m_renderManager ? m_renderManager->scaleFactor() : 1.0;
    info << QString("Scale: %1%").arg(currentScale * 100, 0, 'f', 0);

    QString infoText = info.join(" | ");

    // Draw performance info
    painter.setPen(Qt::white);
    painter.drawText(10, 20, infoText);

    painter.restore();
}

// Note: Mouse and keyboard input handling is now managed by InputHandler
// Events are processed through the InputHandler and forwarded to SessionManager

void ClientRemoteWindow::saveScreenshot(const QString& fileName) {
    if ( m_renderManager ) {
        QPixmap screenshot = m_renderManager->getRemoteScreen();
        if ( !screenshot.isNull() ) {
            QString file = fileName.isEmpty() ? QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")) : fileName;
            screenshot.save(file);
        }
    }
}

// onSceneChanged, updateViewTransform and enableOpenGL are now handled by RenderManager

bool ClientRemoteWindow::isClosing() const {
    return m_isClosing;
}

void ClientRemoteWindow::onWindowResizeRequested(const QSize& size) {
    // 智能窗口大小调整：根据图片宽高比调整窗口大小以消除黑边
    if ( size.isEmpty() ) {
        return;
    }

    // 获取当前窗口的父窗口（主窗口）
    QWidget* parentWindow = window();
    if ( !parentWindow ) {
        return;
    }

    // 计算窗口边框和标题栏的额外空间
    QSize currentWindowSize = parentWindow->size();
    QSize currentViewportSize = viewport()->size();
    QSize extraSpace = currentWindowSize - currentViewportSize;

    // 计算新的窗口大小
    QSize newWindowSize = size + extraSpace;

    // 获取屏幕可用区域，确保窗口不会超出屏幕
    QScreen* screen = parentWindow->screen();
    if ( screen ) {
        QRect availableGeometry = screen->availableGeometry();

        // 限制窗口大小不超过屏幕的80%
        int maxWidth = static_cast<int>(availableGeometry.width() * 0.8);
        int maxHeight = static_cast<int>(availableGeometry.height() * 0.8);

        if ( newWindowSize.width() > maxWidth || newWindowSize.height() > maxHeight ) {
            // 如果计算出的窗口大小太大，按比例缩小
            double scaleX = static_cast<double>(maxWidth) / newWindowSize.width();
            double scaleY = static_cast<double>(maxHeight) / newWindowSize.height();
            double scale = qMin(scaleX, scaleY);

            newWindowSize = QSize(static_cast<int>(newWindowSize.width() * scale),
                static_cast<int>(newWindowSize.height() * scale));
        }

        // 设置最小窗口大小
        newWindowSize = newWindowSize.expandedTo(QSize(400, 300));
    }

    // 调整窗口大小
    parentWindow->resize(newWindowSize);

    // 记录调试信息
    qCDebug(lcClientRemoteWindow) << "Window resize requested:"
        << "requested viewport size:" << size
        << "new window size:" << newWindowSize;
}