#include "FloatingRemoteToolbar.h"
#include "ClientRemoteWindow.h"
#include "../../common/logging/LoggingCategories.h"

#include <QtWidgets/QToolButton>
#include <QtWidgets/QHBoxLayout>
#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtGui/QIcon>
#include <QtCore/QTimer>

FloatingRemoteToolbar::FloatingRemoteToolbar(QWidget* ownerWindow)
    : QWidget(ownerWindow)       // transient parent 确保 Z-order 正确
    , m_ownerWindow(ownerWindow)
{
    // Qt::Tool 使其成为独立顶层窗口（不被 QOpenGLWidget 遮挡）
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    setupUi();
    hide();

    // ── 隐藏防抖：全屏过渡期间 owner 可能短暂发送 Hide 后立即 Move/Resize
    // 直接用 hide() 会销毁原生窗口；0ms 定时器让事件循环有机会合并取消
    m_hideDebounce = new QTimer(this);
    m_hideDebounce->setSingleShot(true);
    m_hideDebounce->setInterval(200);  // 200ms 覆盖全屏过渡的跨迭代 Hide→Show 序列
    connect(m_hideDebounce, &QTimer::timeout, this, [this]() {
        qCDebug(lcClientRemoteWindow) << "[Toolbar] 防抖定时器触发 → hide()";
        QWidget::hide();
    });

    if (m_ownerWindow) {
        m_ownerWindow->installEventFilter(this);
        connect(m_ownerWindow, &QObject::destroyed, this, [this]() {
            m_ownerWindow = nullptr;
        });
    }

    qCDebug(lcClientRemoteWindow) << "FloatingRemoteToolbar 构造完成";
}

void FloatingRemoteToolbar::setupUi()
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
            this, &FloatingRemoteToolbar::toggleFullscreenRequested);

    m_disconnectBtn = new QToolButton(this);
    m_disconnectBtn->setIcon(QIcon(":/icons/disconnect.svg"));
    m_disconnectBtn->setIconSize(iconSize);
    m_disconnectBtn->setToolTip(tr("断开连接"));
    m_disconnectBtn->setStyleSheet(btnStyle);
    m_disconnectBtn->setCursor(Qt::PointingHandCursor);
    m_disconnectBtn->setFocusPolicy(Qt::NoFocus);
    m_disconnectBtn->setAutoRaise(true);
    connect(m_disconnectBtn, &QToolButton::clicked,
            this, &FloatingRemoteToolbar::disconnectRequested);

    m_toggleViewOnlyBtn = new QToolButton(this);
    m_toggleViewOnlyBtn->setIcon(QIcon(":/icons/eye.svg"));
    m_toggleViewOnlyBtn->setIconSize(iconSize);
    m_toggleViewOnlyBtn->setToolTip(tr("仅查看切换"));
    m_toggleViewOnlyBtn->setStyleSheet(btnStyle);
    m_toggleViewOnlyBtn->setCursor(Qt::PointingHandCursor);
    m_toggleViewOnlyBtn->setFocusPolicy(Qt::NoFocus);
    m_toggleViewOnlyBtn->setAutoRaise(true);
    connect(m_toggleViewOnlyBtn, &QToolButton::clicked,
            this, &FloatingRemoteToolbar::toggleViewOnlyRequested);

    layout->addStretch();
    layout->addWidget(m_toggleFullscreenBtn);
    layout->addWidget(m_disconnectBtn);
    layout->addWidget(m_toggleViewOnlyBtn);
    layout->addStretch();
}

void FloatingRemoteToolbar::setViewOnly(bool viewOnly)
{
    m_viewOnly = viewOnly;
    m_toggleViewOnlyBtn->setIcon(QIcon(m_viewOnly
        ? ":/icons/eye-off.svg" : ":/icons/eye.svg"));
    m_toggleViewOnlyBtn->setToolTip(m_viewOnly
        ? tr("退出仅查看") : tr("仅查看切换"));
}

void FloatingRemoteToolbar::updatePosition()
{
    // 用 width > 0 替代 isVisible()：全屏过渡 / 远程刷新期间 isVisible()
    // 可能短暂返回 false，但窗口几何数据仍然有效。
    if (!m_ownerWindow || m_ownerWindow->width() <= 0) return;

    const int naturalWidth = qMax(layout()->sizeHint().width(), ToolbarHeight * 2);
    const QPoint ownerTopCenter = m_ownerWindow->mapToGlobal(
        QPoint(m_ownerWindow->width() / 2, 0));
    setGeometry(ownerTopCenter.x() - naturalWidth / 2,
                ownerTopCenter.y(),
                naturalWidth, ToolbarHeight);
}

bool FloatingRemoteToolbar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != m_ownerWindow)
        return QWidget::eventFilter(obj, event);

    switch (event->type()) {
    case QEvent::Show:
        m_hideDebounce->stop();   // 取消待处理的隐藏
        m_shown = true;
        updatePosition();
        show();
        raise();
        update();                  // 确保 layered window 重建后首次绘制
        qCDebug(lcClientRemoteWindow)
            << "[Toolbar] Show → shown=" << m_shown
            << "ownerVisible=" << m_ownerWindow->isVisible()
            << "toolbarVisible=" << QWidget::isVisible();
        break;
    case QEvent::Move:
    case QEvent::Resize:
        m_hideDebounce->stop();   // 取消待处理的隐藏
        if (m_shown) {
            updatePosition();
            show();
            raise();
        }
        break;
    case QEvent::Hide:
        // 窗口真正关闭时立刻隐藏无需防抖；全屏过渡的 Hide 则延迟以等待后续 Show
        if (auto* w = qobject_cast<ClientRemoteWindow*>(m_ownerWindow)) {
            if (w->isClosing()) {
                QWidget::hide();
                qCDebug(lcClientRemoteWindow)
                    << "[Toolbar] Hide → 窗口关闭，立即隐藏";
                break;
            }
        }
        // 延迟隐藏：全屏过渡可能短暂发送 Hide 后立即 Move/Resize/Show
        // 直接用 hide() → 销毁原生窗口 → 后续 show() 重建时 layered window
        // 可能处于"已映射但未绘制"状态，导致工具栏不可见但可点击。
        m_hideDebounce->start();
        qCDebug(lcClientRemoteWindow)
            << "[Toolbar] Hide → 启动防抖定时器"
            << "ownerVisible=" << m_ownerWindow->isVisible();
        break;
    default:
        break;
    }

    return QWidget::eventFilter(obj, event);
}

void FloatingRemoteToolbar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 30, 30, 200));
}
