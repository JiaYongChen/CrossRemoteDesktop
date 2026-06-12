#include "ClientRemoteWindow.h"
#include "InputForwarder.h"
#include "ConnectionLifecycle.h"
#include "../managers/SessionManager.h"
#include "../managers/FileTransferManager.h"
#include "../../common/core/config/UiConstants.h"
#include "RenderManager.h"
#include "CursorManager.h"
#include "../../common/clipboard/ClipboardManager.h"

#include <QtGui/QCursor>
#ifndef QT_NO_OPENGL
#include "GLTextureViewport.h"
#endif
#include <QtGui/QPainter>
#include <QtGui/QResizeEvent>
#include <QtGui/QPaintEvent>
#include <QtGui/QCloseEvent>
#include <QtGui/QEnterEvent>
#include <QtWidgets/QMessageBox>
#include <QtGui/QScreen>
#include "../../common/core/logging/LoggingCategories.h"
#include <cmath>


ClientRemoteWindow::ClientRemoteWindow(SessionManager* sessionManager, QWidget* parent)
    : QWidget(parent)
    , m_connectionId(sessionManager ? sessionManager->connectionId() : QString::number(0))
    , m_sessionManager(sessionManager)
    , m_isFullScreen(false)
    , m_isClosing(false) {

    setAttribute(Qt::WA_DeleteOnClose, true);
    qCDebug(lcClientRemoteWindow) << "ClientRemoteWindow::ClientRemoteWindow() - Constructor started";

    // ── Create extracted components ──
    m_inputForwarder = new InputForwarder(this);
    m_inputForwarder->installOn(this);
    m_inputForwarder->setSessionManager(m_sessionManager);

    m_connectionLifecycle = new ConnectionLifecycle(this);
    m_connectionLifecycle->manage(this, m_sessionManager);

    // ── Create sub-managers ──
    initializeManagers();

    configureWindow();
    setupUI();

    setWindowTitle(tr("Remote Desktop"));

    if (m_sessionManager) {
        setupManagerConnections();
    }
}

ClientRemoteWindow::~ClientRemoteWindow() {
    // Qt parent-child cleanup handles everything
}

QString ClientRemoteWindow::connectionId() const {
    return m_connectionId;
}

void ClientRemoteWindow::updateWindowTitle(const QString& title) {
    m_hostName = title;
    if (m_connectionLifecycle) {
        m_connectionLifecycle->setHostName(title);
    }
}

// ── Connection state (delegated) ──

void ClientRemoteWindow::setConnectionState(ConnectionManager::ConnectionState state) {
    if (m_connectionLifecycle) {
        m_connectionLifecycle->setConnectionState(state);
    }
}

ConnectionManager::ConnectionState ClientRemoteWindow::connectionState() const {
    return m_connectionLifecycle ? m_connectionLifecycle->connectionState()
                                : ConnectionManager::Disconnected;
}

// ── Screen display ──

void ClientRemoteWindow::setRemoteScreen(const QImage& image) {
#ifndef QT_NO_OPENGL
    if (m_glViewport) {
        m_glViewport->uploadFrame(image);
    }
#else
    Q_UNUSED(image)
#endif
}

void ClientRemoteWindow::updateRemoteScreen(const QImage& screen) {
    setRemoteScreen(screen);
}

// ── Scaling ──

void ClientRemoteWindow::setScaleFactor(double factor) {
    if (m_renderManager) {
        m_renderManager->setScaleFactor(factor);
    }
}

double ClientRemoteWindow::scaleFactor() const {
    return m_renderManager ? m_renderManager->scaleFactor() : 1.0;
}

// ── Full screen ──

void ClientRemoteWindow::setFullScreen(bool fullScreen) {
    m_isFullScreen = fullScreen;
}

bool ClientRemoteWindow::isFullScreen() const {
    return m_isFullScreen;
}

// ── Input control ──

void ClientRemoteWindow::setInputEnabled(bool enabled) {
    if (m_inputForwarder) {
        m_inputForwarder->setEnabled(enabled);
    }
}

bool ClientRemoteWindow::isInputEnabled() const {
    return true; // delegated to InputForwarder
}

// ── Manager access ──

FileTransferManager* ClientRemoteWindow::fileTransferManager() const {
    return m_fileTransferManager;
}

RenderManager* ClientRemoteWindow::renderManager() const {
    return m_renderManager;
}

CursorManager* ClientRemoteWindow::cursorManager() const {
    return m_cursorManager;
}

// ── Initialization ──

void ClientRemoteWindow::initializeManagers() {
    m_fileTransferManager = new FileTransferManager(this, this);
    m_renderManager = new RenderManager(this, this);
    m_cursorManager = new CursorManager(this, this);
    m_clipboardManager = new ClipboardManager(this);
    m_clipboardManager->setEnabled(true);
}

void ClientRemoteWindow::configureWindow() {
    setMinimumSize(400, 225);
    resize(1600, 900);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true); // still needed even with event filter
}

void ClientRemoteWindow::enableManagerFeatures() {
    if (m_fileTransferManager) {
        m_fileTransferManager->setEnabled(true);
    }
}

void ClientRemoteWindow::setupUI() {
#ifndef QT_NO_OPENGL
    m_glViewport = new GLTextureViewport(this);
    m_glViewport->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_glViewport->setGeometry(rect());
    m_glViewport->show();
    m_glViewport->raise();

    if (m_renderManager) {
        m_renderManager->setGLViewport(m_glViewport);
    }

    // Wire viewport to InputForwarder for coordinate mapping
    if (m_inputForwarder) {
        m_inputForwarder->setViewport(m_glViewport);
    }

    connect(m_glViewport, &GLTextureViewport::renderRectChanged, this, [this](const QRectF&) {
        update();
    });
#endif
}

void ClientRemoteWindow::setupManagerConnections() {
    if (!m_sessionManager) return;

    // Performance stats → overlay refresh
    connect(m_sessionManager, &SessionManager::performanceStatsUpdated,
        this, &ClientRemoteWindow::onPerformanceStatsUpdated);

    // Connection state → lifecycle manager
    connect(m_sessionManager, &SessionManager::connectionStateChanged,
        this, &ClientRemoteWindow::setConnectionState);

    // Cursor type → CursorManager
    if (m_cursorManager) {
        connect(m_sessionManager, &SessionManager::remoteCursorTypeUpdated,
            m_cursorManager, &CursorManager::setRemoteCursorType);
    }

    // Render resize requests
    if (m_renderManager) {
        connect(m_renderManager, &RenderManager::windowResizeRequested,
            this, &ClientRemoteWindow::onWindowResizeRequested);
    }

    // Clipboard sync
    if (m_clipboardManager) {
        connect(m_clipboardManager, &ClipboardManager::clipboardTextChanged,
            this, [this](const QString& text) {
            if (m_sessionManager) {
                m_sessionManager->sendClipboardText(text);
            }
        });

        connect(m_clipboardManager, &ClipboardManager::clipboardImageChanged,
            this, [this](const QByteArray& imageData, quint32 width, quint32 height) {
            if (m_sessionManager) {
                m_sessionManager->sendClipboardImage(imageData, width, height);
            }
        });

        connect(m_sessionManager, &SessionManager::clipboardTextReceived,
            m_clipboardManager, &ClipboardManager::setText);

        connect(m_sessionManager, &SessionManager::clipboardImageReceived,
            m_clipboardManager, &ClipboardManager::setImageFromPng);
    }
}

// ── Event handlers ──

void ClientRemoteWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    if (m_showPerformanceInfo) {
        QPainter painter(this);
        drawPerformanceInfo(painter);
    }
}

void ClientRemoteWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

#ifndef QT_NO_OPENGL
    if (m_glViewport) {
        m_glViewport->setGeometry(rect());
    }
#endif

    if (m_renderManager) {
        m_renderManager->onViewResized();
    }
}

void ClientRemoteWindow::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);
    if (m_cursorManager) {
        m_cursorManager->applyLocalCursorState();
    }
}

void ClientRemoteWindow::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (m_cursorManager) {
        m_cursorManager->restoreLocalCursor();
    }
}

void ClientRemoteWindow::closeEvent(QCloseEvent* event) {
    qCDebug(lcClientRemoteWindow) << "closeEvent received, isClosing=" << m_isClosing;

    if (m_isClosing) {
        event->accept();
        return;
    }

    m_isClosing = true;
    emit windowClosed();

#ifndef QT_NO_OPENGL
    if (m_glViewport) {
        m_glViewport->cleanupGLResources();
    }
#endif

    event->accept();
    QWidget::closeEvent(event);
}

bool ClientRemoteWindow::isClosing() const {
    return m_isClosing;
}

// ── Slots ──

void ClientRemoteWindow::onConnectionClosed() {
    // Placeholder — lifecycle events handled by ConnectionLifecycle
}

void ClientRemoteWindow::onConnectionError(const QString& error) {
    QMessageBox::critical(this, "Connection Error", error);
}

void ClientRemoteWindow::onScreenUpdated(const QImage& screen) {
    updateRemoteScreen(screen);
}

void ClientRemoteWindow::onPerformanceStatsUpdated() {
    if (m_showPerformanceInfo) {
        update();
    }
}

// ── Performance overlay ──

void ClientRemoteWindow::drawPerformanceInfo(QPainter& painter) {
    painter.save();

    QString sessionInfo = m_sessionManager ? m_sessionManager->getFormattedPerformanceInfo() : "No Session";
    QStringList info;
    info << sessionInfo;

    double currentScale = m_renderManager ? m_renderManager->scaleFactor() : 1.0;
    info << QString("Scale: %1%").arg(currentScale * 100, 0, 'f', 0);
    info << QString("Render: OpenGL Direct");

    QString infoText = info.join(" | ");
    painter.setPen(Qt::white);
    painter.drawText(10, 20, infoText);

    painter.restore();
}

// ── Window resize from remote ──

void ClientRemoteWindow::onWindowResizeRequested(const QSize& size) {
    if (size.isEmpty()) return;

    QWidget* parentWindow = window();
    if (!parentWindow) return;

    QSize currentViewportSize = this->size();
    QSize extraSpace = parentWindow->size() - currentViewportSize;
    QSize newWindowSize = size + extraSpace;

    QScreen* screen = parentWindow->screen();
    if (screen) {
        QRect availableGeometry = screen->availableGeometry();
        int maxWidth = static_cast<int>(availableGeometry.width() * 0.8);
        int maxHeight = static_cast<int>(availableGeometry.height() * 0.8);

        if (newWindowSize.width() > maxWidth || newWindowSize.height() > maxHeight) {
            double scaleX = static_cast<double>(maxWidth) / newWindowSize.width();
            double scaleY = static_cast<double>(maxHeight) / newWindowSize.height();
            double scale = qMin(scaleX, scaleY);
            newWindowSize = QSize(static_cast<int>(newWindowSize.width() * scale),
                static_cast<int>(newWindowSize.height() * scale));
        }
        newWindowSize = newWindowSize.expandedTo(QSize(400, 300));
    }

    parentWindow->resize(newWindowSize);
    qCDebug(lcClientRemoteWindow) << "Window resize requested:" << size << "→" << newWindowSize;
}
