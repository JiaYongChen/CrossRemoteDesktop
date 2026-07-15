#include "FloatingRemoteToolbar.h"
#include "../../common/logging/LoggingCategories.h"

#include <QtWidgets/QToolButton>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGraphicsOpacityEffect>
#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtGui/QIcon>
#include <QtCore/QPropertyAnimation>
#include <QtCore/QParallelAnimationGroup>
#include <QtCore/QEasingCurve>

FloatingRemoteToolbar::FloatingRemoteToolbar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(ToolbarHeight);
    setAutoFillBackground(false);
    setupUi();
    setupAnimation();

    move(width() / 2, -ToolbarHeight);  // 初始位置在视口外、不可见
    m_opacityEffect->setOpacity(0.0);   // 初始透明

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
    painter.fillRect(rect(), QColor(30, 30, 30, 200));
}

void FloatingRemoteToolbar::setupAnimation()
{
    // 透明度效果
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    // Y 坐标动画
    m_yAnim = new QPropertyAnimation(this, "toolbarY");
    m_yAnim->setDuration(250);
    m_yAnim->setEasingCurve(QEasingCurve::OutCubic);

    // 透明度动画
    m_opacityAnim = new QPropertyAnimation(m_opacityEffect, "opacity");
    m_opacityAnim->setDuration(250);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    // 并行动画组
    m_animGroup = new QParallelAnimationGroup(this);
    m_animGroup->addAnimation(m_yAnim);
    m_animGroup->addAnimation(m_opacityAnim);
}

int FloatingRemoteToolbar::toolbarY() const
{
    return pos().y();
}

void FloatingRemoteToolbar::setToolbarY(int y)
{
    move(x(), y);
}

void FloatingRemoteToolbar::showAnimated()
{
    m_animGroup->stop();
    m_yAnim->setStartValue(toolbarY());
    m_yAnim->setEndValue(0);
    m_yAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_opacityAnim->setStartValue(m_opacityEffect->opacity());
    m_opacityAnim->setEndValue(1.0);
    m_animGroup->start();
}

void FloatingRemoteToolbar::hideAnimated()
{
    m_animGroup->stop();
    m_yAnim->setStartValue(toolbarY());
    m_yAnim->setEndValue(-ToolbarHeight);
    m_yAnim->setEasingCurve(QEasingCurve::InCubic);
    m_opacityAnim->setStartValue(m_opacityEffect->opacity());
    m_opacityAnim->setEndValue(0.0);
    m_animGroup->start();
}

bool FloatingRemoteToolbar::isAnimating() const
{
    return m_animGroup && m_animGroup->state() == QAbstractAnimation::Running;
}
