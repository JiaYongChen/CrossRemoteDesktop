#include "FullscreenToolbar.h"
#include "../../common/logging/LoggingCategories.h"

#include <QtWidgets/QToolButton>
#include <QtWidgets/QHBoxLayout>
#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QEnterEvent>

FullscreenToolbar::FullscreenToolbar(QWidget* parentWindow)
    : QWidget(parentWindow)        // transient parent 确保 Z-order 在 owner 之上
    , m_ownerWindow(parentWindow)
{
    // Qt::Tool 使其成为独立顶层窗口（不被 QOpenGLWidget 遮挡），
    // 同时保持 transient parent 关系确保全屏时 Z-order 正确。
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    setupUi();
    hide();

    if (m_ownerWindow) {
        m_ownerWindow->setMouseTracking(true);
        m_ownerWindow->installEventFilter(this);
        // owner 销毁时清空裸指针；Qt parent-child 自动析构 toolbar
        connect(m_ownerWindow, &QObject::destroyed, this, [this]() {
            m_ownerWindow = nullptr;
            m_showDelayTimer->stop();
            m_autoHideTimer->stop();
        });
    }

    qCDebug(lcClientRemoteWindow) << "FullscreenToolbar 构造完成";

    m_showDelayTimer = new QTimer(this);
    m_showDelayTimer->setSingleShot(true);
    connect(m_showDelayTimer, &QTimer::timeout,
            this, &FullscreenToolbar::onShowDelayTimeout);

    m_autoHideTimer = new QTimer(this);
    m_autoHideTimer->setSingleShot(true);
    connect(m_autoHideTimer, &QTimer::timeout,
            this, &FullscreenToolbar::onAutoHideTimeout);

    m_deactivateTimer = new QTimer(this);
    m_deactivateTimer->setSingleShot(true);
    m_deactivateTimer->setInterval(200);
    connect(m_deactivateTimer, &QTimer::timeout,
            this, &FullscreenToolbar::onDeactivateDelay);
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
        << "visible:" << m_toolbarVisible
        << "owner:" << (m_ownerWindow ? "ok" : "null");

    if (m_toolbarVisible) return;
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

void FullscreenToolbar::onDeactivateDelay()
{
    // 200ms 后 owner 仍未激活 → 确认为 Alt+Tab，隐藏工具栏
    if (m_ownerWindow && !m_ownerWindow->isActiveWindow()) {
        hideToolbar();
    }
}

bool FullscreenToolbar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != m_ownerWindow) {
        return QWidget::eventFilter(obj, event);
    }

    // 工具栏可见时：跟随移动、窗口失焦时延迟隐藏、owner 隐藏时跟随
    if (m_toolbarVisible) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
            updatePosition();
            break;
        case QEvent::Hide:
            hideToolbar();
            break;
        case QEvent::WindowDeactivate:
            // 延迟隐藏：区分 Alt+Tab（持续失活）和全屏切换（瞬态失活）
            m_deactivateTimer->start();
            break;
        case QEvent::WindowActivate:
            // 全屏切换后立即恢复，取消延迟隐藏
            m_deactivateTimer->stop();
            break;
        default:
            break;
        }
        return QWidget::eventFilter(obj, event);
    }

    // 工具栏隐藏时检测触发区
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
