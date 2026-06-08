#include "ClientRemoteWindow.h"
#include "../network/TcpClient.h"
#include "../managers/SessionManager.h"
#include "../managers/FileTransferManager.h"
#include "../../common/core/config/UiConstants.h"
#include "../../common/core/config/MessageConstants.h"
#include "RenderManager.h"
#include "CursorManager.h"
#include "../../common/clipboard/ClipboardManager.h"

#include <QtGui/QCursor>
#ifndef QT_NO_OPENGL
#include "GLTextureViewport.h"
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
#include "../../common/core/logging/LoggingCategories.h"
#include <QtWidgets/QMenuBar>
#include <QtGui/QKeySequence>
#include <QtGui/QIcon>
#include <QtCore/QThread>
#include <cmath>


ClientRemoteWindow::ClientRemoteWindow(SessionManager* sessionManager, QWidget* parent)
    : QWidget(parent)
    , m_connectionId(QString::number(0))
    , m_sessionManager(sessionManager)
    , m_connectionState(ConnectionManager::Disconnected)
    , m_isFullScreen(false)
    , m_isClosing(false)
    , m_hostName()
    , m_inputEnabled(true)
    , m_lastMousePos(-1, -1)
    , m_fileTransferManager(nullptr)
    , m_renderManager(nullptr)
    , m_cursorManager(nullptr)
    , m_showPerformanceInfo(false) {
    // Ensure auto-delete on close to avoid parentless window memory leak
    setAttribute(Qt::WA_DeleteOnClose, true);
    qCDebug(lcClientRemoteWindow) << "ClientRemoteWindow::ClientRemoteWindow() - Constructor started for sessionManager:" << sessionManager;

    m_connectionId = sessionManager ? sessionManager->connectionId() : QString::number(0);

    // Initialize all managers using composition pattern
    initializeManagers();

    // Configure window properties (size, title, etc.)
    configureWindow();

    // Setup UI components
    setupUI();

    // Window title will be set externally via updateWindowTitle()
    setWindowTitle(tr("Remote Desktop"));

    // Setup connections to SessionManager if provided
    if ( m_sessionManager ) {
        setupManagerConnections();
    }
}

ClientRemoteWindow::~ClientRemoteWindow() {
    // Note: do not emit windowClosed() in destructor, to avoid duplication with closeEvent
    // Qt parent-child relationship will clean up children automatically
}

QString ClientRemoteWindow::connectionId() const {
    return m_connectionId;
}

// Window title management
void ClientRemoteWindow::updateWindowTitle(const QString& title) {
    if ( !title.isEmpty() ) {
        m_hostName = title;
        setWindowTitle(title);
    }
}

void ClientRemoteWindow::updateWindowTitle() {
    if ( !m_hostName.isEmpty() ) {
        QString title;
        switch ( m_connectionState ) {
            case ConnectionManager::Connecting:
                title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_CONNECTING);
                break;
            case ConnectionManager::Connected:
                title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_CONNECTED);
                break;
            case ConnectionManager::Authenticating:
                title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_AUTHENTICATING);
                break;
            case ConnectionManager::Authenticated:
                title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_AUTHENTICATED);
                break;
            case ConnectionManager::Disconnecting:
                title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_DISCONNECTING);
                break;
            case ConnectionManager::Disconnected:
                title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_DISCONNECTED);
                break;
            case ConnectionManager::Reconnecting:
                title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_RECONNECTING);
                break;
            case ConnectionManager::Error:
                title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_ERROR);
                break;
            default:
                title = m_hostName;
                break;
        }
        setWindowTitle(title);
    }
}

void ClientRemoteWindow::initializeManagers() {
    // File transfer management
    m_fileTransferManager = new FileTransferManager(this, this);

    // Render and view management
    m_renderManager = new RenderManager(this, this);

    // Cursor management — plain QWidget, no viewport indirection needed
    m_cursorManager = new CursorManager(this, this);

    // Clipboard management
    m_clipboardManager = new ClipboardManager(this);
    m_clipboardManager->setEnabled(true);
}

void ClientRemoteWindow::configureWindow() {
    setMinimumSize(400, 225);
    resize(1600, 900);

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void ClientRemoteWindow::enableManagerFeatures() {
    if ( m_fileTransferManager ) {
        m_fileTransferManager->setEnabled(true);
    }
}

void ClientRemoteWindow::setupUI() {
    // GLTextureViewport fills the entire window and is the sole render surface.
    // All QGraphicsView/QGraphicsScene/QGraphicsPixmapItem infrastructure removed.
#ifndef QT_NO_OPENGL
    m_glViewport = new GLTextureViewport(this);
    m_glViewport->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_glViewport->setGeometry(rect());
    m_glViewport->show();
    m_glViewport->raise();

    // Wire GL viewport into RenderManager for coordinate mapping
    if ( m_renderManager ) {
        m_renderManager->setGLViewport(m_glViewport);
    }

    // Connect render rect changes for overlay invalidation
    connect(m_glViewport, &GLTextureViewport::renderRectChanged, this, [this](const QRectF&) {
        update();
    });
#endif
}

void ClientRemoteWindow::setupManagerConnections() {
    if ( m_sessionManager ) {
        connect(m_sessionManager, &SessionManager::performanceStatsUpdated,
            this, &ClientRemoteWindow::onPerformanceStatsUpdated);

        connect(m_sessionManager, &SessionManager::connectionStateChanged,
            this, &ClientRemoteWindow::setConnectionState);
    }

    if ( m_cursorManager && m_sessionManager ) {
        connect(m_sessionManager, &SessionManager::remoteCursorTypeUpdated,
            m_cursorManager, &CursorManager::setRemoteCursorType);
    }

    if ( m_renderManager ) {
        connect(m_renderManager, &RenderManager::windowResizeRequested,
            this, &ClientRemoteWindow::onWindowResizeRequested);
    }

    if ( m_clipboardManager && m_sessionManager ) {
        connect(m_clipboardManager, &ClipboardManager::clipboardTextChanged,
            this, [this](const QString& text) {
            if ( m_sessionManager && m_connectionState == ConnectionManager::Connected ) {
                m_sessionManager->sendClipboardText(text);
            }
        });

        connect(m_clipboardManager, &ClipboardManager::clipboardImageChanged,
            this, [this](const QByteArray& imageData, quint32 width, quint32 height) {
            if ( m_sessionManager && m_connectionState == ConnectionManager::Connected ) {
                m_sessionManager->sendClipboardImage(imageData, width, height);
            }
        });

        connect(m_sessionManager, &SessionManager::clipboardTextReceived,
            m_clipboardManager, &ClipboardManager::setText);

        connect(m_sessionManager, &SessionManager::clipboardImageReceived,
            m_clipboardManager, &ClipboardManager::setImageFromPng);
    }
}

// Connection state management
void ClientRemoteWindow::setConnectionState(ConnectionManager::ConnectionState state) {
    if ( m_connectionState != state ) {
        ConnectionManager::ConnectionState oldState = m_connectionState;
        m_connectionState = state;
        updateWindowTitle();

        if ( state == ConnectionManager::Disconnected && !m_isClosing ) {
            if ( oldState == ConnectionManager::Connected ||
                oldState == ConnectionManager::Authenticated ||
                oldState == ConnectionManager::Authenticating ||
                oldState == ConnectionManager::Error ) {

                qCInfo(lcClientRemoteWindow) << "ClientRemoteWindow::setConnectionState() - Connection lost, preparing to show notification and close window";

                QTimer::singleShot(100, this, [this]() {
                    if ( !m_isClosing ) {
                        showDisconnectionDialog();
                    }
                });
            }
        }
    }
}

ConnectionManager::ConnectionState ClientRemoteWindow::connectionState() const {
    return m_connectionState;
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

// Input control
void ClientRemoteWindow::setInputEnabled(bool enabled) {
    m_inputEnabled = enabled;
}

bool ClientRemoteWindow::isInputEnabled() const {
    return m_inputEnabled;
}

// Manager access methods
FileTransferManager* ClientRemoteWindow::fileTransferManager() const {
    return m_fileTransferManager;
}

RenderManager* ClientRemoteWindow::renderManager() const {
    return m_renderManager;
}

CursorManager* ClientRemoteWindow::cursorManager() const {
    return m_cursorManager;
}

// Event handlers
void ClientRemoteWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    // GLTextureViewport handles all rendering.
    // Only draw performance overlay if enabled.
    if ( m_showPerformanceInfo ) {
        QPainter painter(this);
        drawPerformanceInfo(painter);
    }
}

void ClientRemoteWindow::mousePressEvent(QMouseEvent* event) {
    if ( m_inputEnabled && m_sessionManager ) {
        QPoint remotePos = mapToRemote(event->pos());

        int mouseEventType = 0;
        Qt::MouseButton btn = event->button();

        if ( btn == Qt::LeftButton ) {
            mouseEventType = static_cast<int>(MouseEventType::LEFT_PRESS);
        } else if ( btn == Qt::RightButton ) {
            mouseEventType = static_cast<int>(MouseEventType::RIGHT_PRESS);
        } else if ( btn == Qt::MiddleButton ) {
            mouseEventType = static_cast<int>(MouseEventType::MIDDLE_PRESS);
        }

        if ( mouseEventType != 0 ) {
            QMetaObject::invokeMethod(m_sessionManager, "sendMouseEvent",
                Qt::QueuedConnection,
                Q_ARG(int, remotePos.x()),
                Q_ARG(int, remotePos.y()),
                Q_ARG(int, mouseEventType));
        }
    }
    QWidget::mousePressEvent(event);

    if ( m_cursorManager ) {
        m_cursorManager->refreshLocalCursor();
    }
}

void ClientRemoteWindow::mouseReleaseEvent(QMouseEvent* event) {
    if ( m_inputEnabled && m_sessionManager ) {
        QPoint remotePos = mapToRemote(event->pos());

        int mouseEventType = 0;
        Qt::MouseButton btn = event->button();

        if ( btn == Qt::LeftButton ) {
            mouseEventType = static_cast<int>(MouseEventType::LEFT_RELEASE);
        } else if ( btn == Qt::RightButton ) {
            mouseEventType = static_cast<int>(MouseEventType::RIGHT_RELEASE);
        } else if ( btn == Qt::MiddleButton ) {
            mouseEventType = static_cast<int>(MouseEventType::MIDDLE_RELEASE);
        }

        if ( mouseEventType != 0 ) {
            QMetaObject::invokeMethod(m_sessionManager, "sendMouseEvent",
                Qt::QueuedConnection,
                Q_ARG(int, remotePos.x()),
                Q_ARG(int, remotePos.y()),
                Q_ARG(int, mouseEventType));
        }
    }
    QWidget::mouseReleaseEvent(event);

    if ( m_cursorManager ) {
        m_cursorManager->refreshLocalCursor();
    }
}

void ClientRemoteWindow::mouseMoveEvent(QMouseEvent* event) {
    if ( m_inputEnabled && m_sessionManager ) {
        QPoint remotePos = mapToRemote(event->pos());

        QMetaObject::invokeMethod(m_sessionManager, "sendMouseEvent",
            Qt::QueuedConnection,
            Q_ARG(int, remotePos.x()),
            Q_ARG(int, remotePos.y()),
            Q_ARG(int, static_cast<int>(MouseEventType::MOVE)));
    }
    QWidget::mouseMoveEvent(event);

    if ( m_cursorManager ) {
        m_cursorManager->refreshLocalCursor();
    }
}

void ClientRemoteWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if ( m_inputEnabled && m_sessionManager ) {
        QPoint remotePos = mapToRemote(event->pos());

        int mouseEventType = 0;
        Qt::MouseButton btn = event->button();

        if ( btn == Qt::LeftButton ) {
            mouseEventType = static_cast<int>(MouseEventType::LEFT_DOUBLE_CLICK);
        } else if ( btn == Qt::RightButton ) {
            mouseEventType = static_cast<int>(MouseEventType::RIGHT_DOUBLE_CLICK);
        } else if ( btn == Qt::MiddleButton ) {
            mouseEventType = static_cast<int>(MouseEventType::MIDDLE_DOUBLE_CLICK);
        }

        if ( mouseEventType != 0 ) {
            QMetaObject::invokeMethod(m_sessionManager, "sendMouseEvent",
                Qt::QueuedConnection,
                Q_ARG(int, remotePos.x()),
                Q_ARG(int, remotePos.y()),
                Q_ARG(int, mouseEventType));
        }
    }
    QWidget::mouseDoubleClickEvent(event);

    if ( m_cursorManager ) {
        m_cursorManager->refreshLocalCursor();
    }
}

void ClientRemoteWindow::wheelEvent(QWheelEvent* event) {
    if ( m_inputEnabled && m_sessionManager ) {
        QPoint remotePos = mapToRemote(event->position().toPoint());
        int delta = event->angleDelta().y();

        QMetaObject::invokeMethod(m_sessionManager, "sendWheelEvent",
            Qt::QueuedConnection,
            Q_ARG(int, remotePos.x()),
            Q_ARG(int, remotePos.y()),
            Q_ARG(int, delta),
            Q_ARG(int, Qt::Vertical));
    }
    QWidget::wheelEvent(event);
}

void ClientRemoteWindow::keyPressEvent(QKeyEvent* event) {
#ifndef QT_NO_OPENGL
    // Toggle VSync with Ctrl+V (GL mode only)
    if ( event->key() == Qt::Key_V
         && (event->modifiers() & Qt::ControlModifier) ) {
        if ( m_glViewport ) {
            m_glViewport->setVSyncEnabled(!m_glViewport->isVSyncEnabled());
            qCInfo(lcClientRemoteWindow) << "VSync toggled via Ctrl+V:"
                << (m_glViewport->isVSyncEnabled() ? "ON" : "OFF");
            event->accept();
            return;
        }
    }
#endif

    if ( m_inputEnabled && m_sessionManager ) {
        QMetaObject::invokeMethod(m_sessionManager, "sendKeyboardEvent",
            Qt::QueuedConnection,
            Q_ARG(int, event->key()),
            Q_ARG(int, static_cast<int>(event->modifiers())),
            Q_ARG(bool, true),
            Q_ARG(QString, event->text()));
    }
    QWidget::keyPressEvent(event);
}

void ClientRemoteWindow::keyReleaseEvent(QKeyEvent* event) {
    if ( m_inputEnabled && m_sessionManager ) {
        QMetaObject::invokeMethod(m_sessionManager, "sendKeyboardEvent",
            Qt::QueuedConnection,
            Q_ARG(int, event->key()),
            Q_ARG(int, static_cast<int>(event->modifiers())),
            Q_ARG(bool, false),
            Q_ARG(QString, QString()));
    }
    QWidget::keyReleaseEvent(event);
}

void ClientRemoteWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // Keep GL viewport filling the window
#ifndef QT_NO_OPENGL
    if ( m_glViewport ) {
        m_glViewport->setGeometry(rect());
    }
#endif

    // Notify RenderManager of view resize for coordinate recalculation
    if ( m_renderManager ) {
        m_renderManager->onViewResized();
    }
}

void ClientRemoteWindow::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
}

void ClientRemoteWindow::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
}

void ClientRemoteWindow::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);

    if ( m_cursorManager ) {
        m_cursorManager->applyLocalCursorState();
    }
}

void ClientRemoteWindow::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);

    if ( m_cursorManager ) {
        m_cursorManager->restoreLocalCursor();
    }
}

void ClientRemoteWindow::closeEvent(QCloseEvent* event) {
    qCDebug(lcClientRemoteWindow) << "closeEvent received, isClosing=" << m_isClosing;

    if ( m_isClosing ) {
        event->accept();
        return;
    }

    m_isClosing = true;

    emit windowClosed();

    // 在 QWidget::closeEvent（内部调用 hide()）之前主动清理 GL 资源。
    // hide() 在 Windows 上会销毁原生 QWindow，导致后续析构时
    // QOpenGLWidget::makeCurrent() 因缺失有效 surface 而崩溃。
    // 此处 GL 资源清理完成后，析构函数会检测 m_glCleanedUp 标记并跳过重复清理。
#ifndef QT_NO_OPENGL
    if ( m_glViewport ) {
        m_glViewport->cleanupGLResources();
    }
#endif

    event->accept();
    QWidget::closeEvent(event);
}

// Private slots
void ClientRemoteWindow::onConnectionClosed() {
    // Connection closed callback — no state changes here
}

void ClientRemoteWindow::onConnectionError(const QString& error) {
    QMessageBox::critical(this, "Connection Error", error);
}

void ClientRemoteWindow::showDisconnectionDialog() {
    qCInfo(lcClientRemoteWindow) << "ClientRemoteWindow::showDisconnectionDialog() - Showing disconnection dialog";

    m_isClosing = true;

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Connection Disconnected"));
    msgBox.setText(tr("Connection to remote host %1 has been disconnected.").arg(m_hostName.isEmpty() ? "Server" : m_hostName));
    msgBox.setInformativeText(tr("The window will close."));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);

    msgBox.exec();

    qCInfo(lcClientRemoteWindow) << "ClientRemoteWindow::showDisconnectionDialog() - User confirmed disconnect, closing window";

    close();
}

void ClientRemoteWindow::onPerformanceStatsUpdated() {
    if ( m_showPerformanceInfo ) {
        update();
    }
}

QPoint ClientRemoteWindow::mapToRemote(const QPoint& localPoint) const {
    // Delegate to GL viewport directly for coordinate mapping
#ifndef QT_NO_OPENGL
    if ( m_glViewport ) {
        return m_glViewport->mapToRemote(localPoint);
    }
#else
    Q_UNUSED(localPoint)
#endif
    return QPoint(0, 0);
}

QPoint ClientRemoteWindow::mapFromRemote(const QPoint& remotePoint) const {
#ifndef QT_NO_OPENGL
    if ( m_glViewport ) {
        return m_glViewport->mapFromRemote(remotePoint);
    }
#else
    Q_UNUSED(remotePoint)
#endif
    return QPoint(0, 0);
}

QRect ClientRemoteWindow::mapToRemote(const QRect& localRect) const {
    QPoint topLeft = mapToRemote(localRect.topLeft());
    QPoint bottomRight = mapToRemote(localRect.bottomRight());
    return QRect(topLeft, bottomRight);
}

QRect ClientRemoteWindow::mapFromRemote(const QRect& remoteRect) const {
    QPoint topLeft = mapFromRemote(remoteRect.topLeft());
    QPoint bottomRight = mapFromRemote(remoteRect.bottomRight());
    return QRect(topLeft, bottomRight);
}

void ClientRemoteWindow::drawPerformanceInfo(QPainter& painter) {
    painter.save();

    QString sessionInfo = m_sessionManager ? m_sessionManager->getFormattedPerformanceInfo() : "No Session";

    QStringList info;
    info << sessionInfo;

    double currentScale = m_renderManager ? m_renderManager->scaleFactor() : 1.0;
    info << QString("Scale: %1%").arg(currentScale * 100, 0, 'f', 0);

    // GL is always active now
    info << QString("Render: OpenGL Direct");

    QString infoText = info.join(" | ");

    painter.setPen(Qt::white);
    painter.drawText(10, 20, infoText);

    painter.restore();
}

bool ClientRemoteWindow::isClosing() const {
    return m_isClosing;
}

void ClientRemoteWindow::onWindowResizeRequested(const QSize& size) {
    if ( size.isEmpty() ) {
        return;
    }

    QWidget* parentWindow = window();
    if ( !parentWindow ) {
        return;
    }

    // Calculate extra space from window borders and title bar
    QSize currentWindowSize = parentWindow->size();
    // For QWidget (no viewport), use widget's own size as the content area
    QSize currentViewportSize = this->size();
    QSize extraSpace = currentWindowSize - currentViewportSize;

    QSize newWindowSize = size + extraSpace;

    QScreen* screen = parentWindow->screen();
    if ( screen ) {
        QRect availableGeometry = screen->availableGeometry();

        int maxWidth = static_cast<int>(availableGeometry.width() * 0.8);
        int maxHeight = static_cast<int>(availableGeometry.height() * 0.8);

        if ( newWindowSize.width() > maxWidth || newWindowSize.height() > maxHeight ) {
            double scaleX = static_cast<double>(maxWidth) / newWindowSize.width();
            double scaleY = static_cast<double>(maxHeight) / newWindowSize.height();
            double scale = qMin(scaleX, scaleY);

            newWindowSize = QSize(static_cast<int>(newWindowSize.width() * scale),
                static_cast<int>(newWindowSize.height() * scale));
        }

        newWindowSize = newWindowSize.expandedTo(QSize(400, 300));
    }

    parentWindow->resize(newWindowSize);

    qCDebug(lcClientRemoteWindow) << "Window resize requested:"
        << "requested size:" << size
        << "new window size:" << newWindowSize;
}
