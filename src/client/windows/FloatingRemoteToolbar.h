#pragma once

#include <QtWidgets/QWidget>

class QToolButton;
class QTimer;

/// 浮动远程桌面工具栏 — 始终悬浮在远程窗口上方，提供全屏切换/断开/仅查看操作。
///
/// 作为独立顶层窗口（Qt::Tool）通过 transient parent 关系保持在 owner 之上。
/// 跟随 owner 的 Show/Hide/Move/Resize 事件自动显隐和定位。
class FloatingRemoteToolbar : public QWidget {
    Q_OBJECT
public:
    explicit FloatingRemoteToolbar(QWidget* ownerWindow);

    /// 同步仅查看按钮图标
    void setViewOnly(bool viewOnly);

signals:
    void toggleFullscreenRequested();
    void disconnectRequested();
    void toggleViewOnlyRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUi();
    void updatePosition();

    QWidget*     m_ownerWindow         = nullptr;
    QToolButton* m_toggleFullscreenBtn = nullptr;
    QToolButton* m_disconnectBtn       = nullptr;
    QToolButton* m_toggleViewOnlyBtn   = nullptr;
    bool         m_viewOnly            = false;
    bool         m_shown               = false;
    QTimer*      m_hideDebounce        = nullptr;

    static constexpr int ToolbarHeight = 28;
};
