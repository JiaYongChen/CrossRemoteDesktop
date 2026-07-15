#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/Qt>
#include <QtCore/QString>
#include <QtCore/QPoint>
#include "../network/ConnectionManager.h"

// Forward declarations
class QMouseEvent;
class QKeyEvent;
class QWheelEvent;
class QResizeEvent;
class QFocusEvent;
class QCloseEvent;
class QEnterEvent;
class QEvent;
class QLabel;
class GLTextureViewport;
class ProtocolSession;
class CursorManager;
class ClipboardManager;
class InputForwarder;
class ConnectionLifecycle;
class FloatingRemoteToolbar;

class ClientRemoteWindow : public QWidget {
    Q_OBJECT

public:
    explicit ClientRemoteWindow(ProtocolSession* sessionManager, QWidget* parent = nullptr);
    ~ClientRemoteWindow();

    // Connection identification
    QString connectionId() const;

    // Window title management
    void updateWindowTitle(const QString& title);

    // Connection state (delegated to ConnectionLifecycle)
    void setConnectionState(ConnectionManager::ConnectionState state);
    ConnectionManager::ConnectionState connectionState() const;

    // Screen display (delegated to GLTextureViewport)
    void setRemoteScreen(const QImage& image);
    void updateRemoteScreen(const QImage& screen);

    // Scaling
    void setScaleFactor(double factor);
    double scaleFactor() const;

    void setFullScreen(bool fullScreen);
    bool isFullScreen() const;

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

    // GL viewport access
    GLTextureViewport* glViewport() const { return m_glViewport; }

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

    QString m_connectionId;
    ProtocolSession* m_protocolSession;
    bool m_isFullScreen;
    bool m_isClosing;

    // Cached host name for title display
    QString m_hostName;

    // 剪贴板共享标志（供 toggleViewOnly 恢复时使用）
    bool m_shareClipboard = true;

    // Scale factor (replaces RenderManager scaling)
    double m_scaleFactor = 1.0;

    // Sub-managers (owned via Qt parent-child)
    CursorManager* m_cursorManager = nullptr;
    ClipboardManager* m_clipboardManager = nullptr;

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
