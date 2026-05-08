#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/Qt>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QList>
#include <QtCore/QDateTime>
#include <QtCore/QPoint>
#include <QtCore/QSize>
#include <QtCore/QRect>
#include "../network/ConnectionManager.h"
#include "RenderManager.h"

// Forward declarations to reduce compile dependencies
class QWidget;
class QTimer;
class QPainter;
class QMouseEvent;
class QKeyEvent;
class QWheelEvent;
class QPaintEvent;
class QResizeEvent;
class QDragEnterEvent;
class QDropEvent;
class QFocusEvent;
class QCloseEvent;
class GLTextureViewport;

class SessionManager;
class FileTransferManager;
class RenderManager;
class CursorManager;
class ClipboardManager;

class ClientRemoteWindow : public QWidget {
    Q_OBJECT

public:
    explicit ClientRemoteWindow(SessionManager* sessionManager, QWidget* parent = nullptr);
    ~ClientRemoteWindow();

    // Connection identification
    QString connectionId() const;

    // Window title management
    void updateWindowTitle(const QString& title);

    // Connection state management
    void setConnectionState(ConnectionManager::ConnectionState state);
    ConnectionManager::ConnectionState connectionState() const;

    // Screen display methods (delegated to GLTextureViewport)
    void setRemoteScreen(const QImage& image);
    void updateRemoteScreen(const QImage& screen);

    // Scaling
    void setScaleFactor(double factor);
    double scaleFactor() const;

    void setFullScreen(bool fullScreen);
    bool isFullScreen() const;

    // Input control
    void setInputEnabled(bool enabled);
    bool isInputEnabled() const;

    // Manager access methods
    FileTransferManager* fileTransferManager() const;
    RenderManager* renderManager() const;
    CursorManager* cursorManager() const;

    // GL viewport access
    GLTextureViewport* glViewport() const { return m_glViewport; }

    // Query whether the window is in closing flow
    // Set when closeEvent is triggered; external callers (e.g. ClientManager)
    // use this to decide whether to call close() again to avoid re-entry deadlock.
    bool isClosing() const;

signals:
    // Lifecycle events
    void windowClosed();

protected:
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onConnectionClosed();
    void onConnectionError(const QString& error);

    void onScreenUpdated(const QImage& screen);
    void onPerformanceStatsUpdated();

    void onWindowResizeRequested(const QSize& size);

private:
    void updateWindowTitle(); // Update title using cached host name
    void initializeManagers();
    void configureWindow();
    void enableManagerFeatures();

    void setupManagerConnections();
    void setupUI();

    QPoint mapToRemote(const QPoint& localPoint) const;
    QPoint mapFromRemote(const QPoint& remotePoint) const;
    QRect mapToRemote(const QRect& viewRect) const;
    QRect mapFromRemote(const QRect& remoteRect) const;

    void drawPerformanceInfo(QPainter& painter);

    // Show disconnection dialog
    void showDisconnectionDialog();

private:
    QString m_connectionId;
    SessionManager* m_sessionManager;
    ConnectionManager::ConnectionState m_connectionState;
    bool m_isFullScreen;

    // Window closing flag
    bool m_isClosing;

    // Cached host name, avoids cross-thread direct SessionManager access
    QString m_hostName;

    bool m_inputEnabled;
    QPoint m_lastMousePos;

    FileTransferManager* m_fileTransferManager;
    RenderManager* m_renderManager;
    CursorManager* m_cursorManager;
    ClipboardManager* m_clipboardManager;

    bool m_showPerformanceInfo;

    // GL texture viewport — sole render surface, fills entire widget
    GLTextureViewport* m_glViewport = nullptr;
};
