#include "ClientRemoteWindow.h"
#include "InputForwarder.h"
#include "ConnectionLifecycle.h"
#include "../session/ProtocolSession.h"
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


ClientRemoteWindow::ClientRemoteWindow(ProtocolSession* sessionManager, QWidget* parent)
    : QWidget(parent)
    , m_connectionId(sessionManager ? sessionManager->connectionId() : QString::number(0))
    , m_protocolSession(sessionManager)
    , m_isFullScreen(false)
    , m_isClosing(false) {

    setAttribute(Qt::WA_DeleteOnClose, true);
    qCDebug(lcClientRemoteWindow) << "ClientRemoteWindow::ClientRemoteWindow() - Constructor started";

    // ── Create extracted components ──
    m_inputForwarder = new InputForwarder(this);
    m_inputForwarder->installOn(this);
    m_inputForwarder->setProtocolSession(m_protocolSession);

    m_connectionLifecycle = new ConnectionLifecycle(this);
    m_connectionLifecycle->manage(this);

    // ── Create sub-managers ──
    initializeManagers();

    configureWindow();
    setupUI();

    setWindowTitle(tr("Remote Desktop"));

    if (m_protocolSession) {
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
    if (m_isFullScreen == fullScreen) return;
    m_isFullScreen = fullScreen;
    if (fullScreen) {
        setWindowState(windowState() | Qt::WindowFullScreen);
    } else {
        setWindowState(windowState() & ~Qt::WindowFullScreen);
    }
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
    m_cursorManager = new CursorManager(this);
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
    m_glViewport->setGeometry(rect());
    m_glViewport->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_glViewport->show();
    m_glViewport->raise();

    // Wire viewport to InputForwarder: event filter + coordinate mapping
    if (m_inputForwarder) {
        m_inputForwarder->installOn(m_glViewport);
        m_inputForwarder->setViewport(m_glViewport);
    }

    connect(m_glViewport, &GLTextureViewport::renderRectChanged, this, [this](const QRectF&) {
        update();
    });
#endif
}

void ClientRemoteWindow::setupManagerConnections() {
    // 所有信号接线已由 RemoteDesktopSession::wireSignals() 集中管理
    // 保留空壳以兼容现有调用点
    Q_UNUSED(m_protocolSession);
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

void ClientRemoteWindow::mouseMoveEvent(QMouseEvent* event) {
    QWidget::mouseMoveEvent(event);
    if (m_cursorManager) {
        m_cursorManager->setCursorPosition(event->pos().x(), event->pos().y());
    }
}

void ClientRemoteWindow::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);
    setCursor(Qt::BlankCursor);  // 隐藏本地系统光标
}

void ClientRemoteWindow::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    unsetCursor();  // 恢复本地系统光标
    // 清除本地位置标记，让光标回退到远端坐标——
    // 支持「观看第三方操作服务端」场景（本地鼠标在窗口外）。
    if (m_cursorManager) {
        m_cursorManager->clearLocalPosition();
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

