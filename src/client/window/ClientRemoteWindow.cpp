#include "ClientRemoteWindow.h"

#include <QtGui/QCloseEvent>
#include <QtGui/QCursor>
#include <QtGui/QEnterEvent>
#include <QtGui/QIcon>
#include <QtGui/QResizeEvent>
#include <QtGui/QScreen>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>

#include "client/session/ProtocolSession.h"
#include "client/window/ConnectionLifecycle.h"
#include "client/window/CursorManager.h"
#include "client/window/FloatingRemoteToolbar.h"
#include "client/window/GLTextureViewport.h"
#include "client/window/InputForwarder.h"
#include "common/clipboard/ClipboardManager.h"
#include "common/logging/LoggingCategories.h"


ClientRemoteWindow::ClientRemoteWindow(ProtocolSession* protocolSession, QWidget* parent)
    : QWidget(parent)
    , m_protocolSession(protocolSession)
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

    // ── 全屏悬浮工具栏（必须在 setupUI 之前创建——setupUI 中引用）──
    m_floatingToolbar = new FloatingRemoteToolbar(this);
    connect(m_floatingToolbar, &FloatingRemoteToolbar::toggleFullscreenRequested,
            this, [this]() { setFullScreen(!isFullScreen()); });
    connect(m_floatingToolbar, &FloatingRemoteToolbar::disconnectRequested,
            this, [this]() { close(); });
    connect(m_floatingToolbar, &FloatingRemoteToolbar::toggleViewOnlyRequested,
            this, &ClientRemoteWindow::toggleViewOnly);

    setupUI();

    setWindowTitle(tr("Remote Desktop"));
}


ClientRemoteWindow::~ClientRemoteWindow() {
    // Qt parent-child cleanup handles everything
}

void ClientRemoteWindow::updateWindowTitle(const QString& title) {
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

// ── Full screen ──

void ClientRemoteWindow::setFullScreen(bool fullScreen) {
    // 直接以 QWidget::isFullScreen()（windowState 实时值）为准，无镜像缓存
    if (isFullScreen() == fullScreen) return;
    if (fullScreen) {
        setWindowState(windowState() | Qt::WindowFullScreen);
    } else {
        setWindowState(windowState() & ~Qt::WindowFullScreen);
    }
}

void ClientRemoteWindow::setShareClipboard(bool enabled) {
    m_shareClipboard = enabled;
}

void ClientRemoteWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        if (m_isClosing) {
            QWidget::changeEvent(event);
            return;
        }
        // 窗口状态切换后强制 GL 重绘（无视 m_textureDirty）
        if (m_glViewport) {
            m_glViewport->forceRepaint();
        }
        // 全屏过渡期间原生窗口可能重建 → 子控件 Z 序可能重置。
        // 不调用 show()：子控件可见性由父窗口自动管理，强制 show()
        // 会覆盖将来可能的用户隐藏逻辑。
        if (m_floatingToolbar) {
            m_floatingToolbar->raise();
            repositionToolbar();
        }
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

    m_glViewport = new GLTextureViewport(this);
    m_glViewport->setGeometry(rect());
    m_glViewport->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_glViewport->show();
    m_glViewport->raise();

    // 确保仅查看角标渲染在 GL 视口之上
    m_viewOnlyOverlay->raise();

    // Wire viewport to InputForwarder: event filter + coordinate mapping
    if (m_inputForwarder) {
        m_inputForwarder->installOn(m_glViewport);
        m_inputForwarder->setViewport(m_glViewport);
    }

    connect(m_glViewport, &GLTextureViewport::renderRectChanged, this, [this](const QRectF&) {
        update();
    });

    // 工具栏作为子控件渲染在最顶层——初始不可见，由 hover 热区触发 showAnimated
    m_floatingToolbar->raise();
}

// ── Event handlers ──

void ClientRemoteWindow::repositionToolbar() {
    if (!m_floatingToolbar) return;
    constexpr int minWidth = 2 * FloatingRemoteToolbar::ToolbarHeight;
    const int naturalWidth = qMax(m_floatingToolbar->sizeHint().width(), minWidth);
    const int x = (width() - naturalWidth) / 2;
    m_floatingToolbar->setGeometry(x, 0, naturalWidth,
                                   FloatingRemoteToolbar::ToolbarHeight);
    m_floatingToolbar->raise();  // Z 序保护：resize 后保持在 GL 视口之上
}

void ClientRemoteWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    if (m_glViewport) {
        m_glViewport->setGeometry(rect());
    }

    if (m_floatingToolbar) {
        repositionToolbar();
    }
}

void ClientRemoteWindow::mouseMoveEvent(QMouseEvent* event) {
    // ── 工具栏 hover 热区检测（顶部 10px × 工具栏宽度）──
    if (!m_isClosing) {
        const bool inHotZone = [&]() {
            if (event->pos().y() > 10) return false;
            if (!m_floatingToolbar) return false;
            const int barLeft = m_floatingToolbar->x();
            const int barRight = barLeft + m_floatingToolbar->width();
            return event->pos().x() >= barLeft && event->pos().x() <= barRight;
        }();
        if (inHotZone != m_toolbarHovering) {
            m_toolbarHovering = inHotZone;
            if (m_floatingToolbar) {
                if (inHotZone) {
                    m_floatingToolbar->showAnimated();
                } else {
                    m_floatingToolbar->hideAnimated();
                }
            }
        }
    }

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
    if (m_isClosing) return;
    // 若因进入子控件（工具栏）触发 leave，不恢复光标
    if (m_floatingToolbar && m_floatingToolbar->isVisible()) {
        const QPoint localPos = mapFromGlobal(QCursor::pos());
        if (m_floatingToolbar->geometry().contains(localPos))
            return;
    }
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


