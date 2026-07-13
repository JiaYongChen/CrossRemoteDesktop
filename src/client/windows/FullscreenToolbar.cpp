#include "FullscreenToolbar.h"
#include "../../common/logging/LoggingCategories.h"

#include <QtWidgets/QToolButton>
#include <QtWidgets/QHBoxLayout>
#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QEnterEvent>

FullscreenToolbar::FullscreenToolbar(QWidget* parentWindow)
    : QWidget(parentWindow)
{
    setupUi();
    hide();

    if (parentWindow) {
        parentWindow->setMouseTracking(true);
        parentWindow->installEventFilter(this);
    }

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
    setFixedHeight(TOOLBAR_HEIGHT);
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

void FullscreenToolbar::showToolbar()
{
    if (!m_active || m_toolbarVisible) return;

    QWidget* p = parentWidget();
    if (p) {
        setGeometry(0, 0, p->width(), TOOLBAR_HEIGHT);
    }

    show();
    raise();
    m_toolbarVisible = true;

    qCDebug(lcClientRemoteWindow) << "FullscreenToolbar: 已显示";
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
    showToolbar();
    m_autoHideTimer->start(AUTO_HIDE_MS);
}

void FullscreenToolbar::onAutoHideTimeout()
{
    hideToolbar();
}

bool FullscreenToolbar::eventFilter(QObject* obj, QEvent* event)
{
    // 工具栏不可用或已可见时不检测触发区
    if (!m_active || m_toolbarVisible) {
        return QWidget::eventFilter(obj, event);
    }

    if (event->type() == QEvent::MouseMove) {
        const auto* me = static_cast<QMouseEvent*>(event);
        if (me->pos().y() <= TRIGGER_HEIGHT) {
            if (!m_showDelayTimer->isActive()) {
                m_showDelayTimer->start(SHOW_DELAY_MS);
            }
        } else if (m_showDelayTimer->isActive()) {
            m_showDelayTimer->stop();
        }
    } else if (event->type() == QEvent::Leave) {
        // 鼠标离开窗口时取消延迟弹出计时
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
        m_autoHideTimer->start(AUTO_HIDE_MS);
    }
}
