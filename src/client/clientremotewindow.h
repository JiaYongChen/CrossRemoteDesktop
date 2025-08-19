#ifndef CLIENTREMOTEWINDOW_H
#define CLIENTREMOTEWINDOW_H

#include <QtWidgets/QGraphicsView>
#include <QtCore/Qt>
#include <QtGui/QPixmap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QList>
#include <QtCore/QDateTime>
#include <QtCore/QPoint>
#include <QtCore/QSize>
#include <QtCore/QRect>

// 前置声明以减少编译依赖
class QGraphicsScene;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QMenu;
class QAction;
class QWidget;
class QClipboard;
class QTimer;
class QPainter;
class QMouseEvent;
class QKeyEvent;
class QWheelEvent;
class QPaintEvent;
class QResizeEvent;
class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
class QFocusEvent;
class QCloseEvent;

class TcpClient;
class SessionManager;

class ClientRemoteWindow : public QGraphicsView
{
    Q_OBJECT
    
public:
    enum ViewMode {
        FitToWindow,
        ActualSize,
        CustomScale,
        FillWindow
    };
    Q_ENUM(ViewMode)
    
    enum ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting
    };
    Q_ENUM(ConnectionState)
    
    explicit ClientRemoteWindow(const QString &connectionId, QWidget *parent = nullptr);
    ~ClientRemoteWindow();
    
    QString getConnectionId() const;
    
    // Connection state management
    void setConnectionState(ConnectionState state);
    ConnectionState connectionState() const;
    
    // Client and session management
    void setTcpClient(TcpClient *client);
    
    // Session manager
    void setSessionManager(SessionManager *sessionManager);
    
    // Screen display methods
    void setRemoteScreen(const QPixmap &pixmap);
    void updateRemoteScreen(const QPixmap &screen);
    void updateRemoteRegion(const QPixmap &region, const QRect &rect);
    
    // View mode and scaling
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const;
    
    void setScaleFactor(double factor);
    double scaleFactor() const;
    
    void setFullScreen(bool fullScreen);
    bool isFullScreen() const;
    
    // Input control
    void setInputEnabled(bool enabled);
    bool isInputEnabled() const;
    
    void setKeyboardGrabbed(bool grabbed);
    bool isKeyboardGrabbed() const;
    
    void setMouseGrabbed(bool grabbed);
    bool isMouseGrabbed() const;
    
    // Clipboard synchronization
    void setClipboardSyncEnabled(bool enabled);
    bool isClipboardSyncEnabled() const;
    
    // File transfer
    void setFileTransferEnabled(bool enabled);
    bool isFileTransferEnabled() const;
    
    // Performance settings
    void setFrameRate(int fps);
    int frameRate() const;
    
    void setCompressionLevel(int level);
    int compressionLevel() const;
    
    // Performance monitoring
    double currentFPS() const;
    
    // Session control
    void startSession();
    void pauseSession();
    void resumeSession();
    void terminateSession();
    
signals:
    // Mouse events
    void mousePressed(const QPoint &remotePos, Qt::MouseButton button, Qt::MouseButtons buttons);
    void mouseReleased(const QPoint &remotePos, Qt::MouseButton button, Qt::MouseButtons buttons);
    void mouseMoved(const QPoint &remotePos, Qt::MouseButtons buttons);
    void mouseDoubleClicked(const QPoint &remotePos, Qt::MouseButton button);
    
    // Remote mouse events
    void remoteMousePressed(const QPoint &remotePos, Qt::MouseButton button);
    void remoteMouseReleased(const QPoint &remotePos, Qt::MouseButton button);
    void remoteMouseMoved(const QPoint &remotePos, Qt::MouseButtons buttons);
    
    // Wheel events
    void wheelScrolled(const QPoint &remotePos, int delta, Qt::Orientation orientation);
    void remoteWheelScrolled(const QPoint &remotePos, int delta, Qt::Orientation orientation);
    
    // Keyboard events
    void keyPressed(int key, Qt::KeyboardModifiers modifiers, const QString &text);
    void keyReleased(int key, Qt::KeyboardModifiers modifiers, const QString &text);
    
    // Input events for network transmission
    void mouseEvent(int x, int y, int buttons, int eventType);
    void keyboardEvent(int key, int modifiers, bool pressed, const QString &text);
    void wheelEvent(int x, int y, int delta, int orientation);
    
    // Clipboard events
    void clipboardChanged(const QString &text);
    void clipboardImageChanged(const QPixmap &image);
    
    // File transfer events
    void fileDropped(const QStringList &files, int x, int y);
    void fileTransferRequested(const QString &fileName, const QByteArray &data);
    
    // View events
    void viewModeChanged(ViewMode mode);
    void scaleFactorChanged(double factor);
    void fullScreenToggled(bool fullScreen);
    void viewTransformChanged();
    
    // Connection events
    void disconnectRequested();
    
    void windowClosed();
    
public slots:
    void fitToWindow();
    void actualSize();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void toggleFullScreen();
    void takeScreenshot();
    void showConnectionInfo();
    void showPerformanceStats();
    
protected:
    void closeEvent(QCloseEvent *event) override;
    
    // Event handlers
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    
private slots:
    void onConnectionClosed();
    void onConnectionError(const QString &error);
    void onConnectionStateChanged(int state);
    void onClipboardChanged();
    void updatePerformanceStats();
    
    // Session management slots
    void onSessionStateChanged();
    void onScreenUpdated(const QPixmap &screen);
    void onPerformanceStatsUpdated();
    
    // View update slots
    void onSceneChanged();
    void updateViewTransform();
    
private:
    void setupUI();
    void setupActions();
    void setupContextMenu();
    void setupConnections();
    void setupScene();
    void setupView();
    
    void updateDisplay();
    void calculateScaledSize();
    void updateSceneRect();
    void applyViewMode();
    void updateCursorPosition(const QPoint &position);
    void updateFrameRate();
    QPoint mapToRemote(const QPoint &localPoint) const;
    QPoint mapFromRemote(const QPoint &remotePoint) const;
    QRect mapToRemote(const QRect &viewRect) const;
    QRect mapFromRemote(const QRect &remoteRect) const;
    
    // Cursor management
    void setCursorVisible(bool visible);
    bool isCursorVisible() const;
    void setCursorPosition(const QPoint &position);
    QPoint cursorPosition() const;
    void setCursorPixmap(const QPixmap &pixmap);
    
    // Performance optimization
    void setUpdateMode(QGraphicsView::ViewportUpdateMode mode);
    void enableOpenGL(bool enable = true);
    
    void drawConnectionState(QPainter &painter);
    void drawPerformanceInfo(QPainter &painter);
    void drawCursor(QPainter &painter);
    
    void handleMouseInput(QMouseEvent *event, bool pressed);
    void handleKeyboardInput(QKeyEvent *event, bool pressed);
    
    void saveScreenshot(const QString &fileName = QString());
    void showContextMenu(const QPoint &position);
    
    // Core component
    QString m_connectionId;
    
    // Graphics components
    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_pixmapItem;
    QGraphicsRectItem *m_cursorItem;
    
    // UI components
    QMenu *m_contextMenu;
    
    // Actions
    QAction *m_fitToWindowAction;
    QAction *m_actualSizeAction;
    QAction *m_zoomInAction;
    QAction *m_zoomOutAction;
    QAction *m_fullScreenAction;
    QAction *m_screenshotAction;
    QAction *m_disconnectAction;
    QAction *m_settingsAction;
    
    // Screen data
    QPixmap m_remoteScreen;
    QSize m_remoteSize;
    QSize m_scaledSize;
    
    // State variables
    ConnectionState m_connectionState;
    ViewMode m_viewMode;
    double m_scaleFactor;
    double m_customScaleFactor;
    bool m_isFullScreen;
    
    // Input state
    bool m_inputEnabled;
    bool m_keyboardGrabbed;
    bool m_mouseGrabbed;
    QPoint m_lastMousePos;
    
    // Clipboard
    bool m_clipboardSyncEnabled;
    QClipboard *m_clipboard;
    QString m_lastClipboardText;
    
    // File transfer
    bool m_fileTransferEnabled;
    
    // Performance settings
    int m_frameRate;
    int m_compressionLevel;
    
    // Performance monitoring
    QTimer *m_statsTimer;
    double m_currentFPS;
    QList<QDateTime> m_frameTimes;
    
    // Cursor state
    bool m_cursorVisible;
    QPoint m_cursorPosition;
    QPixmap m_cursorPixmap;
    
    // View state
    bool m_dragging;
    QPoint m_lastPanPoint;
    QTimer *m_updateTimer;
    bool m_pendingUpdate;
    
    // Display options
    bool m_showPerformanceInfo;
    bool m_showCursor;
    
    // Network components
    TcpClient *m_tcpClient;
    SessionManager *m_sessionManager;
};

#endif // CLIENTREMOTEWINDOW_H