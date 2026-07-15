#include "FloatingRemoteToolbar.h"
#include "../../common/logging/LoggingCategories.h"

#include <QtWidgets/QToolButton>
#include <QtWidgets/QHBoxLayout>
#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtGui/QIcon>
#include <QtCore/QPropertyAnimation>
#include <QtCore/QEasingCurve>

FloatingRemoteToolbar::FloatingRemoteToolbar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(0);                  // 初始收起（高度 0）
    setAutoFillBackground(false);
    setupUi();
    setupAnimation();
    hide();                             // isVisible()=false，mouseMoveEvent 首次 hover 触发 showAnimated

    qCDebug(lcClientRemoteWindow) << "FloatingRemoteToolbar 构造完成";
}

void FloatingRemoteToolbar::setupUi()
{
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
    m_disconnectBtn->setAttribute(Qt::WA_TransparentForMouseEvents, false);
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
    m_toggleViewOnlyBtn->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_toggleViewOnlyBtn->setFocusPolicy(Qt::NoFocus);
    m_toggleViewOnlyBtn->setAutoRaise(true);
    connect(m_toggleViewOnlyBtn, &QToolButton::clicked,
            this, &FloatingRemoteToolbar::toggleViewOnlyRequested);

    layout->addWidget(m_toggleFullscreenBtn);
    layout->addWidget(m_disconnectBtn);
    layout->addWidget(m_toggleViewOnlyBtn);
}

void FloatingRemoteToolbar::setViewOnly(bool viewOnly)
{
    m_viewOnly = viewOnly;
    m_toggleViewOnlyBtn->setIcon(QIcon(m_viewOnly
        ? ":/icons/eye-off.svg" : ":/icons/eye.svg"));
    m_toggleViewOnlyBtn->setToolTip(m_viewOnly
        ? tr("退出仅查看") : tr("仅查看切换"));
}

void FloatingRemoteToolbar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(QRect(0, 0, width(), ToolbarHeight), QColor(30, 30, 30, 200));
}

void FloatingRemoteToolbar::setupAnimation()
{
    m_heightAnim = new QPropertyAnimation(this, "toolbarHeight");
    m_heightAnim->setDuration(250);
    m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);
}

int FloatingRemoteToolbar::toolbarHeight() const
{
    return height();
}

void FloatingRemoteToolbar::setToolbarHeight(int h)
{
    setFixedHeight(h);
}

void FloatingRemoteToolbar::showAnimated()
{
    m_heightAnim->stop();
    m_heightAnim->setStartValue(toolbarHeight());
    m_heightAnim->setEndValue(ToolbarHeight);
    m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);
    show();
    m_heightAnim->start();
}

void FloatingRemoteToolbar::hideAnimated()
{
    m_heightAnim->stop();
    m_heightAnim->setStartValue(toolbarHeight());
    m_heightAnim->setEndValue(0);
    m_heightAnim->setEasingCurve(QEasingCurve::InCubic);
    m_heightAnim->start();
}

bool FloatingRemoteToolbar::isAnimating() const
{
    return m_heightAnim && m_heightAnim->state() == QAbstractAnimation::Running;
}
