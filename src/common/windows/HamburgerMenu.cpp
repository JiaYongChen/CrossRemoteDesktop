#include "HamburgerMenu.h"
#include "ui_HamburgerMenu.h"
#include "common/core/theme/IconThemeProvider.h"

#include <QIcon>
#include <QToolButton>

HamburgerMenu::HamburgerMenu(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HamburgerMenu)
{
    setFixedWidth(48);
    setAutoFillBackground(true);

    ui->setupUi(this);

    // 设置图标
    ui->hamburgerButton->setIcon(IconThemeProvider::icon("menu"));
    ui->newConnectionItem->setIcon(IconThemeProvider::icon("new_connection"));
    ui->settingsItem->setIcon(IconThemeProvider::icon("settings"));
    ui->aboutItem->setIcon(IconThemeProvider::icon("about"));
    ui->themeButton->setIcon(IconThemeProvider::icon("theme"));

    // 为动画准备透明度效果（.ui 不支持 QGraphicsOpacityEffect）
    for (auto *btn : {static_cast<QToolButton *>(ui->newConnectionItem),
                      static_cast<QToolButton *>(ui->settingsItem),
                      static_cast<QToolButton *>(ui->aboutItem)}) {
        auto *effect = new QGraphicsOpacityEffect(btn);
        effect->setOpacity(0.0);
        btn->setGraphicsEffect(effect);
        m_itemEffects.append(effect);
    }

    setupAnimations();

    // 信号连接
    connect(ui->hamburgerButton, &QToolButton::clicked, this, [this]() {
        setExpanded(!m_expanded);
    });
    connect(ui->themeButton, &QToolButton::clicked, this, &HamburgerMenu::themeToggled);
    connect(ui->newConnectionItem, &QToolButton::clicked, this, &HamburgerMenu::newConnection);
    connect(ui->settingsItem, &QToolButton::clicked, this, &HamburgerMenu::openSettings);
    connect(ui->aboutItem, &QToolButton::clicked, this, &HamburgerMenu::showAbout);
}

void HamburgerMenu::setupAnimations()
{
    // 测量完整菜单高度
    ui->menuContainer->setMaximumHeight(1000);
    ui->menuContainer->adjustSize();
    m_menuFullHeight = ui->menuContainer->sizeHint().height();
    ui->menuContainer->setMaximumHeight(0);

    m_heightAnimation = new QPropertyAnimation(ui->menuContainer, "maximumHeight", this);
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
    ui->hamburgerButton->setToolTip(tr("菜单"));
    ui->newConnectionItem->setToolTip(tr("新建连接"));
    ui->settingsItem->setToolTip(tr("设置"));
    ui->aboutItem->setToolTip(tr("关于"));
    ui->themeButton->setToolTip(tr("切换主题"));
}

void HamburgerMenu::refreshIcons()
{
    ui->hamburgerButton->setIcon(IconThemeProvider::icon("menu"));
    ui->newConnectionItem->setIcon(IconThemeProvider::icon("new_connection"));
    ui->settingsItem->setIcon(IconThemeProvider::icon("settings"));
    ui->aboutItem->setIcon(IconThemeProvider::icon("about"));
    ui->themeButton->setIcon(IconThemeProvider::icon("theme"));
}
