#pragma once

#include <QtWidgets/QWidget>

class QToolButton;
class QGraphicsOpacityEffect;
class QParallelAnimationGroup;
class QPropertyAnimation;

/// 远程桌面工具栏 — 作为 ClientRemoteWindow 的子控件渲染在 GL 视口之上。
///
/// 提供全屏切换/断开/仅查看操作按钮，通过 paintEvent 绘制半透明背景。
/// 与父窗口的生命周期绑定：父窗口隐藏/销毁时自动跟随，无需独立窗口管理。
class FloatingRemoteToolbar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int toolbarHeight READ toolbarHeight WRITE setToolbarHeight)

public:
    static constexpr int ToolbarHeight = 28;

    explicit FloatingRemoteToolbar(QWidget* parent);

    /// 同步仅查看按钮图标
    void setViewOnly(bool viewOnly);

    void showAnimated();
    void hideAnimated();
    bool isAnimating() const;

signals:
    void toggleFullscreenRequested();
    void disconnectRequested();
    void toggleViewOnlyRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUi();

    int  toolbarHeight() const;
    void setToolbarHeight(int h);
    void setupAnimation();

    QToolButton* m_toggleFullscreenBtn = nullptr;
    QToolButton* m_disconnectBtn       = nullptr;
    QToolButton* m_toggleViewOnlyBtn   = nullptr;
    bool         m_viewOnly            = false;

    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QParallelAnimationGroup* m_animGroup    = nullptr;
    QPropertyAnimation*      m_heightAnim   = nullptr;
    QPropertyAnimation*      m_opacityAnim  = nullptr;
};
