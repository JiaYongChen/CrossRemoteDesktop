#pragma once

#include <QtCore/QString>
#include <QtWidgets/QWidget>

class ClipboardManager;
class ConnectionLifecycle;
class CursorManager;
class DragDropHandler;
class FloatingRemoteToolbar;
class GLTextureViewport;
class InputForwarder;
class ProtocolSession;
class QCloseEvent;
class QEnterEvent;
class QEvent;
class QMouseEvent;
class QResizeEvent;

class ClientRemoteWindow : public QWidget {
    Q_OBJECT

public:
    explicit ClientRemoteWindow(ProtocolSession* protocolSession, QWidget* parent = nullptr);
    ~ClientRemoteWindow();

    // Window title management
    void updateWindowTitle(const QString& title);

    void setFullScreen(bool fullScreen);

    // Input control (delegated to InputForwarder)
    void setInputEnabled(bool enabled);
    bool isInputEnabled() const;

    // View-only mode (delegates to overlay + ConnectionLifecycle)
    void setViewOnly(bool enabled);

    /// 运行时切换仅查看模式（供全屏工具栏调用）
    void toggleViewOnly();

    /// 设置剪贴板共享开关（供 toggleViewOnly 恢复时使用）
    void setShareClipboard(bool enabled);

    // Manager access
    CursorManager* cursorManager() const;

    // DragDropHandler access (for signal wiring)
    DragDropHandler* dragDropHandler() const { return m_dragDropHandler; }

    // GL viewport access
    GLTextureViewport* glViewport() const { return m_glViewport; }

    // ConnectionLifecycle access (for signal wiring)
    ConnectionLifecycle* connectionLifecycle() const { return m_connectionLifecycle; }

    // Query whether the window is in closing flow
    bool isClosing() const;

signals:
    void windowClosed();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void initializeManagers();
    void configureWindow();
    void setupUI();
    void repositionToolbar();

    ProtocolSession* m_protocolSession;
    bool m_isClosing;

    // 剪贴板共享标志（供 toggleViewOnly 恢复时使用）
    bool m_shareClipboard = true;

    // Sub-managers (owned via Qt parent-child)
    CursorManager* m_cursorManager = nullptr;
    ClipboardManager* m_clipboardManager = nullptr;
    DragDropHandler* m_dragDropHandler = nullptr;

    // Extracted responsibilities (owned via Qt parent-child)
    InputForwarder* m_inputForwarder = nullptr;
    ConnectionLifecycle* m_connectionLifecycle = nullptr;

    // View-only overlay (icon + text container)
    QWidget* m_viewOnlyOverlay = nullptr;

    // GL texture viewport — sole render surface, fills entire widget
    GLTextureViewport* m_glViewport = nullptr;

    // 浮动远程桌面工具栏
    FloatingRemoteToolbar* m_floatingToolbar = nullptr;

    bool m_toolbarHovering = false;  // 热区防重入
};
