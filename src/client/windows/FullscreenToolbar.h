#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QTimer>

class QPushButton;

/// 全屏悬浮工具栏 — 鼠标触顶弹出，提供全屏切换/断开/仅查看操作
///
/// 安装为父窗口的事件过滤器，监听顶部 5px 触发区。
/// 300ms 延迟防止误触，5 秒无操作自动隐藏。
class FullscreenToolbar : public QWidget {
    Q_OBJECT
public:
    explicit FullscreenToolbar(QWidget* parentWindow);

    /// 启用/禁用触发区检测（仅在父窗口全屏时启用）
    void setActive(bool active);

    /// 同步仅查看按钮文字
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

private:
    void setupUi();
    void showToolbar();
    void hideToolbar();

    QPushButton* m_toggleFullscreenBtn = nullptr;
    QPushButton* m_disconnectBtn      = nullptr;
    QPushButton* m_toggleViewOnlyBtn  = nullptr;
    QTimer*      m_showDelayTimer     = nullptr;
    QTimer*      m_autoHideTimer      = nullptr;
    bool         m_active             = false;
    bool         m_toolbarVisible     = false;
    bool         m_viewOnly           = false;

    static constexpr int TRIGGER_HEIGHT = 5;
    static constexpr int SHOW_DELAY_MS  = 300;
    static constexpr int AUTO_HIDE_MS   = 5000;
    static constexpr int TOOLBAR_HEIGHT = 32;
};
