#ifndef HAMBURGERMENU_H
#define HAMBURGERMENU_H

#include <QIcon>
#include <QWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>

class HamburgerMenu : public QWidget
{
    Q_OBJECT

public:
    explicit HamburgerMenu(QWidget *parent = nullptr);

    [[nodiscard]] bool isExpanded() const { return m_expanded; }

    void retranslateUi();
    void refreshIcons();

public slots:
    void setExpanded(bool expanded);

signals:
    void newConnection();
    void openSettings();
    void showAbout();
    void themeToggled();

private:
    void setupUi();
    void setupAnimations();
    QToolButton *createMenuItem(const QIcon &icon, const QString &tooltip,
                                 const QString &objectName);

    QToolButton *m_hamburgerButton = nullptr;
    QToolButton *m_themeButton = nullptr;
    QWidget *m_menuContainer = nullptr;
    QVBoxLayout *m_menuLayout = nullptr;

    // 菜单项
    QToolButton *m_newConnectionItem = nullptr;
    QToolButton *m_settingsItem = nullptr;
    QToolButton *m_aboutItem = nullptr;

    // 动画
    QPropertyAnimation *m_heightAnimation = nullptr;
    QParallelAnimationGroup *m_fadeGroup = nullptr;
    QList<QGraphicsOpacityEffect *> m_itemEffects;

    bool m_expanded = false;
    int m_menuFullHeight = 0;
};

#endif // HAMBURGERMENU_H
