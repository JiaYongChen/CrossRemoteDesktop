#include "HamburgerMenu.h"
#include <QIcon>
#include <QFrame>
#include <QApplication>
#include <QTimer>

HamburgerMenu::HamburgerMenu(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("leftNavBar");
    setFixedWidth(48);
    setupUi();
    setupAnimations();
}

void HamburgerMenu::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 8);
    mainLayout->setSpacing(0);

    // ☰ 按钮
    m_hamburgerButton = new QToolButton();
    m_hamburgerButton->setIcon(QIcon(":/icons/menu.svg"));
    m_hamburgerButton->setIconSize(QSize(22, 22));
    m_hamburgerButton->setToolTip(tr("菜单"));
    m_hamburgerButton->setAutoRaise(true);
    m_hamburgerButton->setFixedSize(48, 44);
    mainLayout->addWidget(m_hamburgerButton, 0, Qt::AlignHCenter);

    // 分隔线
    auto *sep1 = new QFrame();
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFixedWidth(35);
    mainLayout->addWidget(sep1, 0, Qt::AlignHCenter);

    // 菜单项容器（动画目标）
    m_menuContainer = new QWidget();
    m_menuContainer->setMaximumHeight(0);  // 默认收起
    m_menuLayout = new QVBoxLayout(m_menuContainer);
    m_menuLayout->setContentsMargins(0, 8, 0, 0);
    m_menuLayout->setSpacing(4);

    // 创建菜单项：图标路径、tooltip、objectName
    m_newConnectionItem = createMenuItem(
        ":/icons/new_connection.svg", tr("新建连接 (Ctrl+N)"),
        "hamburgerItem");

    m_settingsItem = createMenuItem(
        ":/icons/settings.svg", tr("设置 (Ctrl+,)"),
        "hamburgerItem");

    m_aboutItem = createMenuItem(
        ":/icons/about.svg", tr("关于"),
        "hamburgerItem");

    // 添加到菜单布局
    m_menuLayout->addWidget(m_newConnectionItem, 0, Qt::AlignHCenter);
    m_menuLayout->addWidget(m_settingsItem, 0, Qt::AlignHCenter);
    m_menuLayout->addWidget(m_aboutItem, 0, Qt::AlignHCenter);
    m_menuLayout->addStretch();

    mainLayout->addWidget(m_menuContainer);

    // 弹性空间（推开主题按钮到底部）
    mainLayout->addStretch();

    // 主题切换按钮（始终可见）
    m_themeButton = new QToolButton();
    m_themeButton->setIcon(QIcon(":/icons/theme.svg"));
    m_themeButton->setIconSize(QSize(20, 20));
    m_themeButton->setToolTip(tr("切换主题"));
    m_themeButton->setAutoRaise(true);
    m_themeButton->setFixedSize(48, 44);
    mainLayout->addWidget(m_themeButton, 0, Qt::AlignHCenter);

    // 信号连接
    connect(m_hamburgerButton, &QToolButton::clicked, this, [this]() {
        setExpanded(!m_expanded);
    });
    connect(m_themeButton, &QToolButton::clicked, this, &HamburgerMenu::themeToggled);
    connect(m_newConnectionItem, &QToolButton::clicked, this, &HamburgerMenu::newConnection);
    connect(m_settingsItem, &QToolButton::clicked, this, &HamburgerMenu::openSettings);
    connect(m_aboutItem, &QToolButton::clicked, this, &HamburgerMenu::showAbout);
}

QToolButton *HamburgerMenu::createMenuItem(const QString &iconPath,
                                            const QString &tooltip,
                                            const QString &objectName)
{
    auto *btn = new QToolButton();
    btn->setIcon(QIcon(iconPath));
    btn->setIconSize(QSize(20, 20));
    btn->setToolTip(tooltip);
    btn->setAutoRaise(true);
    btn->setObjectName(objectName);
    btn->setFixedSize(40, 40);

    // 为动画准备透明度效果
    auto *effect = new QGraphicsOpacityEffect(btn);
    effect->setOpacity(0.0);
    btn->setGraphicsEffect(effect);
    m_itemEffects.append(effect);

    return btn;
}

void HamburgerMenu::setupAnimations()
{
    // 测量完整菜单高度（hold in temporary state）
    m_menuContainer->setMaximumHeight(1000);
    m_menuContainer->adjustSize();
    m_menuFullHeight = m_menuContainer->sizeHint().height();
    m_menuContainer->setMaximumHeight(0);   // 测量后恢复折叠状态

    m_heightAnimation = new QPropertyAnimation(m_menuContainer, "maximumHeight", this);
    m_heightAnimation->setDuration(150);
    m_heightAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_fadeGroup = new QParallelAnimationGroup(this);
    for (auto *effect : m_itemEffects) {
        auto *fadeAnim = new QPropertyAnimation(effect, "opacity", this);
        fadeAnim->setDuration(100);
        fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_fadeGroup->addAnimation(fadeAnim);
    }
}

void HamburgerMenu::setExpanded(bool expanded)
{
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;

    m_heightAnimation->stop();
    m_fadeGroup->stop();

    if (expanded) {
        // 卷轴下拉 + 图标渐显同步播放
        m_heightAnimation->setStartValue(0);
        m_heightAnimation->setEndValue(m_menuFullHeight);

        m_fadeGroup->setDirection(QAbstractAnimation::Forward);
        for (int i = 0; i < m_fadeGroup->animationCount(); ++i) {
            auto *anim = static_cast<QPropertyAnimation *>(m_fadeGroup->animationAt(i));
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
        }

        m_heightAnimation->start();
        m_fadeGroup->start();
    } else {
        // 图标渐隐 + 卷轴收起同步播放
        m_fadeGroup->setDirection(QAbstractAnimation::Backward);
        for (int i = 0; i < m_fadeGroup->animationCount(); ++i) {
            auto *anim = static_cast<QPropertyAnimation *>(m_fadeGroup->animationAt(i));
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
        }
        m_fadeGroup->start();

        m_heightAnimation->setStartValue(m_menuFullHeight);
        m_heightAnimation->setEndValue(0);
        m_heightAnimation->start();
    }
}

void HamburgerMenu::retranslateUi()
{
    m_hamburgerButton->setToolTip(tr("菜单"));
    m_newConnectionItem->setToolTip(tr("新建连接 (Ctrl+N)"));
    m_settingsItem->setToolTip(tr("设置 (Ctrl+,)"));
    m_aboutItem->setToolTip(tr("关于"));
    m_themeButton->setToolTip(tr("切换主题"));
}
