#include "FullscreenToolbar.h"
#include "../../common/logging/LoggingCategories.h"

#include <QtWidgets/QToolButton>
#include <QtWidgets/QHBoxLayout>
#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QEnterEvent>

FullscreenToolbar::FullscreenToolbar(QWidget* parentWindow)
    : QWidget(nullptr)            // 无 Qt 父控件——顶层窗口
    , m_ownerWindow(parentWindow)  // 记录关联窗口，用于坐标映射
{
    // 设置为无边框顶层弹出窗口，避免被 QOpenGLWidget 渲染内容遮挡。
    // WindowStaysOnTopHint 确保全屏模式下不被 owner 窗口遮挡。
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    setupUi();
    hide();

    if (m_ownerWindow) {
        m_ownerWindow->setMouseTracking(true);
        m_ownerWindow->installEventFilter(this);
        // owner 销毁时清空指针避免 use-after-free，延迟销毁 toolbar
        connect(m_ownerWindow, &QObject::destroyed, this, [this]() {
            m_ownerWindow = nullptr;
            deleteLater();
        });
    }

    qCDebug(lcClientRemoteWindow) << "FullscreenToolbar 构造完成"
        << "active:" << m_active;

    m_showDelayTimer = new QTimer(this);
    m_showDelayTimer->setSingleShot(true);
    connect(m_showDelayTimer, &QTimer::timeout,
            this, &FullscreenToolbar::onShowDelayTimeout);

    m_autoHideTimer = new QTimer(this);
    m_autoHideTimer->setSingleShot(true);
    connect(m_autoHideTimer, &QTimer::timeout,
            this, &FullscreenToolbar::onAutoHideTimeout);
}

void FullscreenToolbar::setupUi()
{
    setFixedHeight(ToolbarHeight);
    setAutoFillBackground(false);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(6);

    const QString btnStyle = QStringLiteral(
        "QToolButton {"
        "  background: rgba(255, 255, 255, 0.08);"
        "  border: none;"
        "  border-radius: 3px;"
        "  padding: 2px;"
        "}"
        "QToolButton:hover {"
        "  background: rgba(255, 255, 255, 0.16);"
        "}"
        "QToolButton:pressed {"
        "  background: rgba(255, 255, 255, 0.22);"
        "}"
    );

    const QSize iconSize(16, 16);

    m_toggleFullscreenBtn = new QToolButton(this);
    m_toggleFullscreenBtn->setIcon(QIcon(":/icons/fullscreen.svg"));
    m_toggleFullscreenBtn->setIconSize(iconSize);
    m_toggleFullscreenBtn->setToolTip(tr("全屏切换"));
    m_toggleFullscreenBtn->setStyleSheet(btnStyle);
    m_toggleFullscreenBtn->setCursor(Qt::PointingHandCursor);
    m_toggleFullscreenBtn->setFocusPolicy(Qt::NoFocus);
    m_toggleFullscreenBtn->setAutoRaise(true);
    connect(m_toggleFullscreenBtn, &QToolButton::clicked,
            this, &FullscreenToolbar::toggleFullscreenRequested);

    m_disconnectBtn = new QToolButton(this);
    m_disconnectBtn->setIcon(QIcon(":/icons/disconnect.svg"));
    m_disconnectBtn->setIconSize(iconSize);
    m_disconnectBtn->setToolTip(tr("断开连接"));
    m_disconnectBtn->setStyleSheet(btnStyle);
    m_disconnectBtn->setCursor(Qt::PointingHandCursor);
    m_disconnectBtn->setFocusPolicy(Qt::NoFocus);
    m_disconnectBtn->setAutoRaise(true);
    connect(m_disconnectBtn, &QToolButton::clicked,
            this, &FullscreenToolbar::disconnectRequested);

    m_toggleViewOnlyBtn = new QToolButton(this);
    m_toggleViewOnlyBtn->setIcon(QIcon(":/icons/eye.svg"));
    m_toggleViewOnlyBtn->setIconSize(iconSize);
    m_toggleViewOnlyBtn->setToolTip(tr("仅查看切换"));
    m_toggleViewOnlyBtn->setStyleSheet(btnStyle);
    m_toggleViewOnlyBtn->setCursor(Qt::PointingHandCursor);
    m_toggleViewOnlyBtn->setFocusPolicy(Qt::NoFocus);
    m_toggleViewOnlyBtn->setAutoRaise(true);
    connect(m_toggleViewOnlyBtn, &QToolButton::clicked,
            this, &FullscreenToolbar::toggleViewOnlyRequested);

    layout->addStretch();
    layout->addWidget(m_toggleFullscreenBtn);
    layout->addWidget(m_disconnectBtn);
    layout->addWidget(m_toggleViewOnlyBtn);
    layout->addStretch();
}

void FullscreenToolbar::setActive(bool active)
{
    m_active = active;
    if (!active) {
        m_showDelayTimer->stop();
        hideToolbar();
    }
}

void FullscreenToolbar::setViewOnly(bool viewOnly)
{
    m_viewOnly = viewOnly;
    m_toggleViewOnlyBtn->setIcon(QIcon(m_viewOnly
        ? ":/icons/eye-off.svg" : ":/icons/eye.svg"));
    m_toggleViewOnlyBtn->setToolTip(m_viewOnly
        ? tr("退出仅查看") : tr("仅查看切换"));
}

void FullscreenToolbar::updatePosition()
{
    if (!m_ownerWindow || !m_ownerWindow->isVisible()) return;

    const int naturalWidth = qMax(layout()->sizeHint().width(), ToolbarHeight * 2);
    // 将 owner 窗口客户区顶部中点映射为全局坐标
    const QPoint ownerTopCenter = m_ownerWindow->mapToGlobal(
        QPoint(m_ownerWindow->width() / 2, 0));
    setGeometry(ownerTopCenter.x() - naturalWidth / 2,
                ownerTopCenter.y(),
                naturalWidth, ToolbarHeight);
}

void FullscreenToolbar::showToolbar()
{
    qCDebug(lcClientRemoteWindow) << "FullscreenToolbar::showToolbar() 入口"
        << "active:" << m_active << "visible:" << m_toolbarVisible
        << "owner:" << (m_ownerWindow ? "ok" : "null")
        << "ownerVisible:" << (m_ownerWindow ? m_ownerWindow->isVisible() : false);

    if (!m_active || m_toolbarVisible) return;
    if (!m_ownerWindow || !m_ownerWindow->isVisible()) return;

    updatePosition();
    show();
    raise();
    m_toolbarVisible = true;

    qCDebug(lcClientRemoteWindow) << "FullscreenToolbar: 已显示"
                                  << "pos:" << pos() << "size:" << size();
}

void FullscreenToolbar::hideToolbar()
{
    if (!m_toolbarVisible) return;

    m_autoHideTimer->stop();
    m_showDelayTimer->stop();
    hide();
    m_toolbarVisible = false;

    qCDebug(lcClientRemoteWindow) << "FullscreenToolbar: 已隐藏";
}

void FullscreenToolbar::onShowDelayTimeout()
{
    qCDebug(lcClientRemoteWindow) << "FullscreenToolbar::onShowDelayTimeout → 调用 showToolbar";
    showToolbar();
    m_autoHideTimer->start(AutoHideMs);
}

void FullscreenToolbar::onAutoHideTimeout()
{
    hideToolbar();
}

bool FullscreenToolbar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != m_ownerWindow) {
        return QWidget::eventFilter(obj, event);
    }

    // 工具栏可见时：跟随移动、窗口失焦时隐藏、owner 隐藏时跟随
    if (m_toolbarVisible) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
            updatePosition();
            break;
        case QEvent::Hide:
        case QEvent::WindowDeactivate:
            hideToolbar();
            break;
        default:
            break;
        }
        return QWidget::eventFilter(obj, event);
    }

    // 工具栏隐藏时检测触发区
    if (!m_active) {
        return QWidget::eventFilter(obj, event);
    }

    if (event->type() == QEvent::MouseMove) {
        const auto* me = static_cast<QMouseEvent*>(event);
        if (me->pos().y() <= TriggerHeight) {
            if (!m_showDelayTimer->isActive()) {
                m_showDelayTimer->start(ShowDelayMs);
            }
        } else if (m_showDelayTimer->isActive()) {
            m_showDelayTimer->stop();
        }
    } else if (event->type() == QEvent::Leave) {
        m_showDelayTimer->stop();
    }

    return QWidget::eventFilter(obj, event);
}

void FullscreenToolbar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(30, 30, 30, 200));
}

void FullscreenToolbar::enterEvent(QEnterEvent* /*event*/)
{
    // 鼠标悬停时停止自动隐藏计时
    m_autoHideTimer->stop();
}

void FullscreenToolbar::leaveEvent(QEvent* /*event*/)
{
    // 鼠标离开后重新开始自动隐藏倒计时
    if (m_toolbarVisible) {
        m_autoHideTimer->start(AutoHideMs);
    }
}
