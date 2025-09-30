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
#include "./managers/ConnectionManager.h"
#include "./managers/RenderManager.h"

// 前置声明以减少编译依赖
class QGraphicsScene;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
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

class SessionManager;
class ClipboardManager;
class FileTransferManager;
class InputHandler;
class CursorManager;
class RenderManager;

class ClientRemoteWindow : public QGraphicsView
{
    Q_OBJECT
    
public:
    explicit ClientRemoteWindow(const QString &connectionId, QWidget *parent = nullptr);
    ~ClientRemoteWindow();
    
    QString getConnectionId() const;
    // 设置连接主机（IP 或主机名）。用于更新窗口标题仅显示 IP/主机。
    // 说明：
    // - 由 ClientManager 在建立连接时调用，传入 connectToHost 的 host
    // - 仅当 host 变化时才刷新标题，避免不必要的 UI 更新
    void setConnectionHost(const QString &host);
    
    // Connection state management
    void setConnectionState(ConnectionManager::ConnectionState state);
    ConnectionManager::ConnectionState connectionState() const;
    
    // Session manager
    void setSessionManager(SessionManager *sessionManager);
    
    // Screen display methods (delegated to RenderManager)
    void setRemoteScreen(const QPixmap &pixmap);
    void updateRemoteScreen(const QPixmap &screen);
    void updateRemoteRegion(const QPixmap &region, const QRect &rect);
    
    // Scaling (delegated to RenderManager)
    
    void setScaleFactor(double factor);
    double scaleFactor() const;
    
    void setFullScreen(bool fullScreen);
    bool isFullScreen() const;
    
    // Image quality and rendering options
    void setImageQuality(RenderManager::ImageQuality quality);
    RenderManager::ImageQuality imageQuality() const;
    
    void enableImageCache(bool enable);
    void clearImageCache();
    void setCacheSizeLimit(int sizeMB);
    
    // Input control
    void setInputEnabled(bool enabled);
    bool isInputEnabled() const;
    
    void setKeyboardGrabbed(bool grabbed);
    bool isKeyboardGrabbed() const;
    
    void setMouseGrabbed(bool grabbed);
    bool isMouseGrabbed() const;
    
    // Manager access methods
    ClipboardManager* clipboardManager() const;
    FileTransferManager* fileTransferManager() const;
    InputHandler* inputHandler() const;
    RenderManager* renderManager() const;
    
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

    // 新增：查询窗口是否处于关闭流程中
    // 说明：
    // - 当 closeEvent 被触发时会设置该标志位，
    // - 用于让外部（如 ClientManager）判断是否需要再次调用 close()，避免重入导致卡死。
    bool isClosing() const;
    
signals:
    // Mouse movement and wheel events used for network transmission or UI updates
    void mouseMoved(const QPoint &remotePos, Qt::MouseButtons buttons);
    void wheelScrolled(const QPoint &remotePos, int delta, Qt::Orientation orientation);
    
    // Input events for network transmission
    void mouseEvent(int x, int y, int buttons, int eventType);
    void keyboardEvent(int key, int modifiers, bool pressed, const QString &text);
    
    // View events

    void scaleFactorChanged(double factor);
    
    // Lifecycle events
    void windowClosed();
    
public slots:
    void toggleFullScreen();
    void takeScreenshot();
    void showConnectionInfo();
    void showPerformanceStats();
    
protected:
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    
private slots:
    void onConnectionClosed();
    void onConnectionError(const QString &error);
    
    void onSessionStateChanged();
    void onScreenUpdated(const QPixmap &screen);
    void onPerformanceStatsUpdated();
    
    void onWindowResizeRequested(const QSize &size);
    
private:
    void initializeManagers();
    void configureWindow();
    void enableManagerFeatures();
    
    void setupManagerConnections();
    void setupScene();
    void setupView();
    
    QPoint mapToRemote(const QPoint &localPoint) const;
    QPoint mapFromRemote(const QPoint &remotePoint) const;
    QRect mapToRemote(const QRect &viewRect) const;
    QRect mapFromRemote(const QRect &remoteRect) const;
    
    CursorManager* cursorManager() const;
    
    void setUpdateMode(QGraphicsView::ViewportUpdateMode mode);
    void enableOpenGL(bool enable = true);
    
    void drawConnectionState(QPainter &painter);
    void drawPerformanceInfo(QPainter &painter);
    
    void saveScreenshot(const QString &fileName = QString());

    QString m_connectionId;
    ConnectionManager::ConnectionState m_connectionState;
    bool m_isFullScreen;

    // 新增：窗口关闭中的标志位
    bool m_isClosing;
    
    bool m_inputEnabled;
    bool m_keyboardGrabbed;
    bool m_mouseGrabbed;
    QPoint m_lastMousePos;
    
    ClipboardManager *m_clipboardManager;
    FileTransferManager *m_fileTransferManager;
    InputHandler *m_inputHandler;
    CursorManager *m_cursorManager;
    RenderManager *m_renderManager;
    
    QPoint m_lastPanPoint;
    
    bool m_showPerformanceInfo;
    
    SessionManager *m_sessionManager;
};

#endif // CLIENTREMOTEWINDOW_H