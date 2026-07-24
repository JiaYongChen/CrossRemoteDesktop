#pragma once

#include <QtCore/QList>
#include <QtCore/QParallelAnimationGroup>
#include <QtCore/QPropertyAnimation>
#include <QtWidgets/QGraphicsOpacityEffect>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class NavPanel; }
QT_END_NAMESPACE

class NavPanel : public QWidget
{
    Q_OBJECT

public:
    explicit NavPanel(QWidget *parent = nullptr);
    ~NavPanel() override;

    void retranslateUi();

public slots:
    void setExpanded(bool expanded);

signals:
    void newConnection();
    void openSettings();
    void showAbout();
    void themeToggled();

private:
    void setupAnimations();

    Ui::NavPanel *ui = nullptr;

    // 动画
    QPropertyAnimation *m_heightAnimation = nullptr;
    QParallelAnimationGroup *m_fadeGroup = nullptr;
    QList<QGraphicsOpacityEffect *> m_itemEffects;

    bool m_expanded = false;
    int m_menuFullHeight = 0;
};

