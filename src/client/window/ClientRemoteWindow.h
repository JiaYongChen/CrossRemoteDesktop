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
class QPaintEvent;
class QResizeEvent;
class QFocusEvent;
class QCloseEvent;
class QEnterEvent;
class QEvent;
class QPainter;
class GLTextureViewport;
class SessionManager;
class CursorManager;
class ClipboardManager;
class InputForwarder;
class ConnectionLifecycle;

class ClientRemoteWindow : public QWidget {
    Q_OBJECT

public:
    explicit ClientRemoteWindow(SessionManager* sessionManager, QWidget* parent = nullptr);
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
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onConnectionClosed();
    void onConnectionError(const QString& error);
    void onScreenUpdated(const QImage& screen);

private:
    void initializeManagers();
    void configureWindow();
    void setupManagerConnections();
    void setupUI();

    QString m_connectionId;
    SessionManager* m_sessionManager;
    bool m_isFullScreen;
    bool m_isClosing;

    // Cached host name for title display
    QString m_hostName;

    // Scale factor (replaces RenderManager scaling)
    double m_scaleFactor = 1.0;

    // Sub-managers (owned via Qt parent-child)
    CursorManager* m_cursorManager = nullptr;
    ClipboardManager* m_clipboardManager = nullptr;

    // Extracted responsibilities (owned via Qt parent-child)
    InputForwarder* m_inputForwarder = nullptr;
    ConnectionLifecycle* m_connectionLifecycle = nullptr;


    // GL texture viewport — sole render surface, fills entire widget
    GLTextureViewport* m_glViewport = nullptr;
};
