#ifndef HAMBURGERMENU_H
#define HAMBURGERMENU_H

#include <QIcon>
#include <QWidget>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QList>

QT_BEGIN_NAMESPACE
namespace Ui { class HamburgerMenu; }
QT_END_NAMESPACE

class HamburgerMenu : public QWidget
{
    Q_OBJECT

public:
    explicit HamburgerMenu(QWidget *parent = nullptr);
    ~HamburgerMenu() override;

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
    void setupAnimations();

    Ui::HamburgerMenu *ui;

    // 动画
    QPropertyAnimation *m_heightAnimation = nullptr;
    QParallelAnimationGroup *m_fadeGroup = nullptr;
    QList<QGraphicsOpacityEffect *> m_itemEffects;

    bool m_expanded = false;
    int m_menuFullHeight = 0;
};

#endif // HAMBURGERMENU_H
