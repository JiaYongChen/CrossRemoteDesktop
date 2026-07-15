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
    setFixedHeight(0);                  // 初始收起（高度 0）
    setAutoFillBackground(false);
    setupUi();
    setupAnimation();

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
    painter.fillRect(QRect(0, 0, width(), ToolbarHeight), QColor(30, 30, 30, 200));
}

void FloatingRemoteToolbar::setupAnimation()
{
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    // 高度动画（展开/收起）——替代 Y 滑动（子控件无法超出父窗口）
    m_heightAnim = new QPropertyAnimation(this, "toolbarHeight");
    m_heightAnim->setDuration(250);
    m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);

    // 透明度动画
    m_opacityAnim = new QPropertyAnimation(m_opacityEffect, "opacity");
    m_opacityAnim->setDuration(250);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_animGroup = new QParallelAnimationGroup(this);
    m_animGroup->addAnimation(m_heightAnim);
    m_animGroup->addAnimation(m_opacityAnim);
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
    m_animGroup->stop();
    m_heightAnim->setStartValue(toolbarHeight());
    m_heightAnim->setEndValue(ToolbarHeight);
    m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_opacityAnim->setStartValue(m_opacityEffect->opacity());
    m_opacityAnim->setEndValue(1.0);
    m_animGroup->start();
}

void FloatingRemoteToolbar::hideAnimated()
{
    m_animGroup->stop();
    m_heightAnim->setStartValue(toolbarHeight());
    m_heightAnim->setEndValue(0);
    m_heightAnim->setEasingCurve(QEasingCurve::InCubic);
    m_opacityAnim->setStartValue(m_opacityEffect->opacity());
    m_opacityAnim->setEndValue(0.0);
    m_animGroup->start();
}

bool FloatingRemoteToolbar::isAnimating() const
{
    return m_animGroup && m_animGroup->state() == QAbstractAnimation::Running;
}
