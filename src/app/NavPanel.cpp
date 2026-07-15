#include "NavPanel.h"
#include "ui_NavPanel.h"
#include "common/theme/IconThemeProvider.h"

#include <QIcon>
#include <QToolButton>

NavPanel::NavPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::NavPanel)
{
    setFixedWidth(48);
    // 使 QWidget 通过样式表绘制背景（需配合 QSS #navPanel 规则）
    setAttribute(Qt::WA_StyledBackground, true);

    ui->setupUi(this);

    // 设置图标
    ui->menuToggleButton->setIcon(IconThemeProvider::icon("menu"));
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
    connect(ui->menuToggleButton, &QToolButton::clicked, this, [this]() {
        setExpanded(!m_expanded);
    });
    connect(ui->themeButton, &QToolButton::clicked, this, &NavPanel::themeToggled);
    connect(ui->newConnectionItem, &QToolButton::clicked, this, &NavPanel::newConnection);
    connect(ui->settingsItem, &QToolButton::clicked, this, &NavPanel::openSettings);
    connect(ui->aboutItem, &QToolButton::clicked, this, &NavPanel::showAbout);
}

NavPanel::~NavPanel()
{
    delete ui;
}

void NavPanel::setupAnimations()
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

void NavPanel::setExpanded(bool expanded)
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

void NavPanel::retranslateUi()
{
    ui->menuToggleButton->setToolTip(tr("菜单"));
    ui->newConnectionItem->setToolTip(tr("新建连接"));
    ui->settingsItem->setToolTip(tr("设置"));
    ui->aboutItem->setToolTip(tr("关于"));
    ui->themeButton->setToolTip(tr("切换主题"));
}
