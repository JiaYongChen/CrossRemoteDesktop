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
#include "./managers/connectionmanager.h"
#include "./managers/rendermanager.h"

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
    enum ViewMode {
        FitToWindow,
        ActualSize,
        CustomScale,
        FillWindow
    };
    Q_ENUM(ViewMode)
    
    explicit ClientRemoteWindow(const QString &connectionId, QWidget *parent = nullptr);
    ~ClientRemoteWindow();
    
    QString getConnectionId() const;
    
    // Connection state management
    void setConnectionState(ConnectionManager::ConnectionState state);
    ConnectionManager::ConnectionState connectionState() const;
    
    // Session manager
    void setSessionManager(SessionManager *sessionManager);
    
    // Screen display methods (delegated to RenderManager)
    void setRemoteScreen(const QPixmap &pixmap);
    void updateRemoteScreen(const QPixmap &screen);
    void updateRemoteRegion(const QPixmap &region, const QRect &rect);
    
    // View mode and scaling (delegated to RenderManager)
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const;
    
    void setScaleFactor(double factor);
    double scaleFactor() const;
    
    void setFullScreen(bool fullScreen);
    bool isFullScreen() const;
    
    // Image quality and rendering options
    void setImageQuality(RenderManager::ImageQuality quality);
    RenderManager::ImageQuality imageQuality() const;
    
    void setAnimationMode(RenderManager::AnimationMode mode);
    RenderManager::AnimationMode animationMode() const;
    
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
    
signals:
    // Mouse movement and wheel events used for network transmission or UI updates
    void mouseMoved(const QPoint &remotePos, Qt::MouseButtons buttons);
    void wheelScrolled(const QPoint &remotePos, int delta, Qt::Orientation orientation);
    
    // Input events for network transmission
    void mouseEvent(int x, int y, int buttons, int eventType);
    void keyboardEvent(int key, int modifiers, bool pressed, const QString &text);
    
    // View events
    void viewModeChanged(ViewMode mode);
    void scaleFactorChanged(double factor);
    
    // Lifecycle events
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
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    
private slots:
    void onConnectionClosed();
    void onConnectionError(const QString &error);
    
    // Session management slots
    void onSessionStateChanged();
    void onScreenUpdated(const QPixmap &screen);
    void onPerformanceStatsUpdated();
    
private:
    // Manager initialization and configuration
    void initializeManagers();
    void configureWindow();
    void enableManagerFeatures();
    
    // Setup methods
    void setupManagerConnections();
    void setupScene();
    void setupView();
    
    // Coordinate mapping (delegated to RenderManager)
    QPoint mapToRemote(const QPoint &localPoint) const;
    QPoint mapFromRemote(const QPoint &remotePoint) const;
    QRect mapToRemote(const QRect &viewRect) const;
    QRect mapFromRemote(const QRect &remoteRect) const;
    
    // Cursor management (delegated to CursorManager)
    CursorManager* cursorManager() const;
    
    // Performance optimization
    void setUpdateMode(QGraphicsView::ViewportUpdateMode mode);
    void enableOpenGL(bool enable = true);
    
    void drawConnectionState(QPainter &painter);
    void drawPerformanceInfo(QPainter &painter);
    
    void saveScreenshot(const QString &fileName = QString());
    
    // Core component
    QString m_connectionId;
    
    // State variables
    ConnectionManager::ConnectionState m_connectionState;
    bool m_isFullScreen;
    
    // Input state
    bool m_inputEnabled;
    bool m_keyboardGrabbed;
    bool m_mouseGrabbed;
    QPoint m_lastMousePos;
    
    // Manager instances
    ClipboardManager *m_clipboardManager;
    FileTransferManager *m_fileTransferManager;
    InputHandler *m_inputHandler;
    CursorManager *m_cursorManager;
    RenderManager *m_renderManager;
    
    // 性能相关
    QPoint m_lastPanPoint;
    
    // Display options
    bool m_showPerformanceInfo;
    
    // Network components
    SessionManager *m_sessionManager;
};

#endif // CLIENTREMOTEWINDOW_H