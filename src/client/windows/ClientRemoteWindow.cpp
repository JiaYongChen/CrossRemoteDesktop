#include "ClientRemoteWindow.h"
#include "InputForwarder.h"
#include "ConnectionLifecycle.h"
#include "FloatingRemoteToolbar.h"
#include "../session/ProtocolSession.h"
#include "CursorManager.h"
#include "../clipboard/ClipboardManager.h"

#include <QtGui/QCursor>
#ifndef QT_NO_OPENGL
#include "GLTextureViewport.h"
#endif
#include <QtGui/QPainter>
#include <QtGui/QResizeEvent>
#include <QtGui/QPaintEvent>
#include <QtGui/QCloseEvent>
#include <QtGui/QEnterEvent>
#include <QtWidgets/QLabel>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMessageBox>
#include <QtGui/QIcon>
#include <QtGui/QScreen>
#include "../../common/logging/LoggingCategories.h"
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

    // ── 全屏悬浮工具栏 ──
    m_floatingToolbar = new FloatingRemoteToolbar(this);
    connect(m_floatingToolbar, &FloatingRemoteToolbar::toggleFullscreenRequested,
            this, [this]() { setFullScreen(!m_isFullScreen); });
    connect(m_floatingToolbar, &FloatingRemoteToolbar::disconnectRequested,
            this, [this]() { close(); });
    connect(m_floatingToolbar, &FloatingRemoteToolbar::toggleViewOnlyRequested,
            this, &ClientRemoteWindow::toggleViewOnly);
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
    if (fullScreen) {
        setWindowState(windowState() | Qt::WindowFullScreen);
    } else {
        setWindowState(windowState() & ~Qt::WindowFullScreen);
    }
}

bool ClientRemoteWindow::isFullScreen() const {
    return m_isFullScreen;
}

void ClientRemoteWindow::setShareClipboard(bool enabled) {
    m_shareClipboard = enabled;
}

void ClientRemoteWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        // 同步 m_isFullScreen 到实际窗口状态（覆盖 setFullScreen 和 OS 触发的切换）
        m_isFullScreen = windowState().testFlag(Qt::WindowFullScreen);
        // 窗口状态切换后强制 GL 重绘（无视 m_textureDirty）
    #ifndef QT_NO_OPENGL
        if (m_glViewport) {
            m_glViewport->forceRepaint();
        }
    #endif
    }
    QWidget::changeEvent(event);
}

void ClientRemoteWindow::toggleViewOnly() {
    bool currentlyViewOnly = !isInputEnabled();
    bool newViewOnly = !currentlyViewOnly;
    setInputEnabled(!newViewOnly);
    setViewOnly(newViewOnly);
    if (m_clipboardManager) {
        m_clipboardManager->setEnabled(m_shareClipboard && !newViewOnly);
    }

    qCInfo(lcClientRemoteWindow) << "View-only mode toggled:"
                                 << (newViewOnly ? "ON" : "OFF");
}

// ── Input control ──

void ClientRemoteWindow::setInputEnabled(bool enabled) {
    if (m_inputForwarder) {
        m_inputForwarder->setEnabled(enabled);
    }
}

bool ClientRemoteWindow::isInputEnabled() const {
    return m_inputForwarder ? m_inputForwarder->isEnabled() : false;
}

void ClientRemoteWindow::setViewOnly(bool enabled) {
    // 叠加层角标
    if (m_viewOnlyOverlay) {
        m_viewOnlyOverlay->setVisible(enabled);
    }
    // 窗口标题后缀（委托给 ConnectionLifecycle）
    if (m_connectionLifecycle) {
        m_connectionLifecycle->setViewOnly(enabled);
    }
    // 同步全屏工具栏按钮状态
    if (m_floatingToolbar) {
        m_floatingToolbar->setViewOnly(enabled);
    }
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
    // ── 仅查看模式叠加层角标 ──
    m_viewOnlyOverlay = new QWidget(this);
    m_viewOnlyOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_viewOnlyOverlay->setStyleSheet(
        "QWidget {"
        "  background: rgba(0, 0, 0, 160);"
        "  border-radius: 6px;"
        "}");

    auto* overlayLayout = new QHBoxLayout(m_viewOnlyOverlay);
    overlayLayout->setContentsMargins(10, 6, 14, 6);
    overlayLayout->setSpacing(6);

    auto* iconLabel = new QLabel();
    iconLabel->setPixmap(QIcon(":/icons/eye.svg").pixmap(16, 16));
    iconLabel->setFixedSize(16, 16);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    iconLabel->setStyleSheet("background: transparent;");

    auto* textLabel = new QLabel(tr("仅查看"));
    textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    textLabel->setStyleSheet(
        "QLabel {"
        "  color: #FFD54F;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  background: transparent;"
        "}");

    overlayLayout->addWidget(iconLabel);
    overlayLayout->addWidget(textLabel);
    m_viewOnlyOverlay->adjustSize();
    m_viewOnlyOverlay->move(12, 12);  // 固定于左上角
    m_viewOnlyOverlay->hide();

#ifndef QT_NO_OPENGL
    m_glViewport = new GLTextureViewport(this);
    m_glViewport->setGeometry(rect());
    m_glViewport->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_glViewport->show();
    m_glViewport->raise();

    // 确保仅查看角标渲染在 GL 视口之上
    m_viewOnlyOverlay->raise();

    // 工具栏作为子控件渲染在最顶层
    m_floatingToolbar->raise();
    m_floatingToolbar->show();
    repositionToolbar();

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

void ClientRemoteWindow::repositionToolbar() {
    if (!m_floatingToolbar) return;
    const int naturalWidth = qMax(m_floatingToolbar->sizeHint().width(), 56);
    const int x = (width() - naturalWidth) / 2;
    m_floatingToolbar->setGeometry(x, 0, naturalWidth, 28);
}

void ClientRemoteWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

#ifndef QT_NO_OPENGL
    if (m_glViewport) {
        m_glViewport->setGeometry(rect());
    }
#endif

    repositionToolbar();  // 窗口大小改变时重新居中工具栏
}

void ClientRemoteWindow::mouseMoveEvent(QMouseEvent* event) {
    QWidget::mouseMoveEvent(event);
    if (m_cursorManager) {
        m_cursorManager->setCursorPosition(event->pos().x(), event->pos().y());
    }
}

void ClientRemoteWindow::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);
    // 仅交互模式下隐藏本地光标；仅查看模式下保留光标，方便用户观察
    if (isInputEnabled()) {
        setCursor(Qt::BlankCursor);
    }
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

