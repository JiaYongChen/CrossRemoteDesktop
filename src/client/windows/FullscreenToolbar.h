#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QTimer>

class QToolButton;

/// 全屏悬浮工具栏 — 鼠标触顶弹出，提供全屏切换/断开/仅查看操作
///
/// 作为独立顶层窗口（Qt::Tool）浮于父窗口上方，避免被 QOpenGLWidget 渲染内容遮挡。
/// 安装为父窗口的事件过滤器，监听顶部 5px 触发区。
/// 300ms 延迟防止误触，5 秒无操作自动隐藏。
class FullscreenToolbar : public QWidget {
    Q_OBJECT
public:
    explicit FullscreenToolbar(QWidget* parentWindow);

    /// 同步仅查看按钮图标
    void setViewOnly(bool viewOnly);

signals:
    void toggleFullscreenRequested();
    void disconnectRequested();
    void toggleViewOnlyRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onShowDelayTimeout();
    void onAutoHideTimeout();
    void onDeactivateDelay();

private:
    void setupUi();
    void showToolbar();
    void hideToolbar();
    void updatePosition();

    /// 记录安装 eventFilter 的目标窗口，用于坐标映射和可见性跟踪
    QWidget*      m_ownerWindow        = nullptr;
    QToolButton*  m_toggleFullscreenBtn = nullptr;
    QToolButton*  m_disconnectBtn       = nullptr;
    QToolButton*  m_toggleViewOnlyBtn   = nullptr;
    QTimer*       m_showDelayTimer      = nullptr;
    QTimer*       m_autoHideTimer       = nullptr;
    QTimer*       m_deactivateTimer     = nullptr;
    bool          m_toolbarVisible      = false;
    bool          m_viewOnly            = false;

    static constexpr int TriggerHeight = 5;
    static constexpr int ShowDelayMs   = 100;
    static constexpr int AutoHideMs    = 2000;
    static constexpr int ToolbarHeight = 28;
};
