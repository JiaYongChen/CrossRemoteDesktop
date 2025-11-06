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
#include "../network/ConnectionManager.h"
#include "RenderManager.h"

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
class FileTransferManager;
class RenderManager;

class ClientRemoteWindow : public QGraphicsView {
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

    // Screen display methods (delegated to RenderManager)
    void setRemoteScreen(const QImage& image);
    void updateRemoteScreen(const QImage& screen);
    void updateRemoteRegion(const QImage& region, const QRect& rect);

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
    FileTransferManager* fileTransferManager() const;
    RenderManager* renderManager() const;

    // Performance settings
    void setFrameRate(int fps);
    int frameRate() const;

    // Performance monitoring
    double currentFPS() const;

    // 新增：查询窗口是否处于关闭流程中
    // 说明：
    // - 当 closeEvent 被触发时会设置该标志位，
    // - 用于让外部（如 ClientManager）判断是否需要再次调用 close()，避免重入导致卡死。
    bool isClosing() const;

signals:
    // Mouse movement and wheel events used for network transmission or UI updates
    void mouseMoved(const QPoint& remotePos, Qt::MouseButtons buttons);
    void wheelScrolled(const QPoint& remotePos, int delta, Qt::Orientation orientation);

    // Input events for network transmission
    void mouseEvent(int x, int y, int buttons, int eventType);
    void keyboardEvent(int key, int modifiers, bool pressed, const QString& text);

    // View events

    void scaleFactorChanged(double factor);

    // Lifecycle events
    void windowClosed();

public slots:
    void toggleFullScreen();
    void takeScreenshot();
    void showConnectionInfo();
    void showPerformanceStats();
    
    // 远程光标控制
    void updateRemoteCursorPosition(const QPoint& position);
    void setShowRemoteCursor(bool show);

protected:
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private slots:
    void onConnectionClosed();
    void onConnectionError(const QString& error);

    void onScreenUpdated(const QImage& screen);
    void onPerformanceStatsUpdated();

    void onWindowResizeRequested(const QSize& size);

private:
    void updateWindowTitle(); // 使用当前连接的主机名更新标题
    void initializeManagers();
    void configureWindow();
    void enableManagerFeatures();

    void setupManagerConnections();
    void setupScene();
    void setupView();

    QPoint mapToRemote(const QPoint& localPoint) const;
    QPoint mapFromRemote(const QPoint& remotePoint) const;
    QRect mapToRemote(const QRect& viewRect) const;
    QRect mapFromRemote(const QRect& remoteRect) const;

    void setUpdateMode(QGraphicsView::ViewportUpdateMode mode);
    void enableOpenGL(bool enable = true);

    void drawPerformanceInfo(QPainter& painter);

    void saveScreenshot(const QString& fileName = QString());

private:
    QString m_connectionId;
    SessionManager* m_sessionManager;
    ConnectionManager::ConnectionState m_connectionState;
    bool m_isFullScreen;

    // 新增：窗口关闭中的标志位
    bool m_isClosing;

    // 缓存主机名，避免跨线程直接调用 SessionManager
    QString m_hostName;

    bool m_inputEnabled;
    bool m_keyboardGrabbed;
    bool m_mouseGrabbed;
    QPoint m_lastMousePos;

    FileTransferManager* m_fileTransferManager;
    RenderManager* m_renderManager;

    QPoint m_lastPanPoint;

    bool m_showPerformanceInfo;
    
    // 远程光标位置和显示控制
    QPoint m_remoteCursorPos;
    bool m_showRemoteCursor;
};

#endif // CLIENTREMOTEWINDOW_H