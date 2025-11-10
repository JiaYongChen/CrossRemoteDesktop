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
class CursorManager;
class ClipboardManager;

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

    // Manager access methods
    FileTransferManager* fileTransferManager() const;
    RenderManager* renderManager() const;
    CursorManager* cursorManager() const;

    // 新增：查询窗口是否处于关闭流程中
    // 说明：
    // - 当 closeEvent 被触发时会设置该标志位，
    // - 用于让外部（如 ClientManager）判断是否需要再次调用 close()，避免重入导致卡死。
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

    // 显示断开连接对话框
    void showDisconnectionDialog();

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
    QPoint m_lastMousePos;

    FileTransferManager* m_fileTransferManager;
    RenderManager* m_renderManager;
    CursorManager* m_cursorManager;
    ClipboardManager* m_clipboardManager;

    bool m_showPerformanceInfo;
};

#endif // CLIENTREMOTEWINDOW_H