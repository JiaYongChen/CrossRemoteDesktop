#include "ClientRemoteWindow.h"
#include "InputForwarder.h"
#include "ConnectionLifecycle.h"
#include "../managers/SessionManager.h"
#include "../../common/core/config/UiConstants.h"
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
    m_connectionLifecycle->manage(this);

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
        m_glViewport->setRemoteScreen(image);
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
    if (factor > 0.0) {
        m_scaleFactor = factor;
    }
}

double ClientRemoteWindow::scaleFactor() const {
    return m_scaleFactor;
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


CursorManager* ClientRemoteWindow::cursorManager() const {
    return m_cursorManager;
}

// ── Initialization ──

void ClientRemoteWindow::initializeManagers() {
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

void ClientRemoteWindow::setupUI() {
#ifndef QT_NO_OPENGL
    m_glViewport = new GLTextureViewport(this);
    m_glViewport->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_glViewport->setGeometry(rect());
    m_glViewport->show();
    m_glViewport->raise();

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


    // Connection state → lifecycle manager
    connect(m_sessionManager, &SessionManager::connectionStateChanged,
        this, &ClientRemoteWindow::setConnectionState);

    // Cursor type → CursorManager
    if (m_cursorManager) {
        connect(m_sessionManager, &SessionManager::remoteCursorTypeUpdated,
            m_cursorManager, &CursorManager::setRemoteCursorType);
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
}

void ClientRemoteWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

#ifndef QT_NO_OPENGL
    if (m_glViewport) {
        m_glViewport->setGeometry(rect());
    }
#endif

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

