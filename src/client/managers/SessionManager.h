#pragma once

#include <QtCore/QObject>
#include <QtCore/QMutex>
#include <QtGui/QPixmap>
#include <QtGui/QImage>
#include <QtCore/QDateTime>
#include <QtCore/QQueue>
#include <QtCore/QSize>
#include "../../common/core/network/Protocol.h"
#include "../../common/core/config/UiConstants.h"
#include "../network/ConnectionManager.h"
#include "../core/TripleBuffer.h"
#include "../core/FrameSlot.h"
#include <atomic>
#include <chrono>

class QTimer;

#ifndef QT_NO_OPENGL
class GLTextureViewport;
class QOpenGLContext;
class QOffscreenSurface;
#endif

class SessionManager : public QObject {
    Q_OBJECT

public:
    struct PerformanceStats {
        double currentFPS;
        QDateTime sessionStartTime;
        int frameCount;
    };

    explicit SessionManager(const QString& connectionId, QObject* parent = nullptr);
    ~SessionManager();

    // 连接ID
    QString connectionId() const;

public slots:
    // 会话控制（跨线程调用需要使用 slots）
    void startSession();
    void suspendSession();
    void resumeSession();
    void terminateSession();
    void moveGLToThread(QThread* target);   ///< 将 GL 对象移到目标线程（为安全析构做准备）

public:
    // 状态查询
    bool isActive() const;

    // 远程桌面数据
    QSize remoteScreenSize() const;

public slots:
    // 输入事件发送（跨线程调用需要使用 slots）
    void sendMouseEvent(int x, int y, int eventType);
    void sendKeyboardEvent(int key, int modifiers, bool pressed, const QString& text);
    void sendWheelEvent(int x, int y, int delta, int orientation);

    // 剪贴板同步（跨线程调用需要使用 slots）
    void sendClipboardText(const QString& text);
    void sendClipboardImage(const QByteArray& imageData, quint32 width, quint32 height);

    // 配置（跨线程调用需要使用 slots）
    void setFrameRate(int fps);

public:
    // 性能统计
    PerformanceStats performanceStats() const;
    void resetStats();

    // 性能信息格式化
    QString getFormattedPerformanceInfo() const;

    // 配置
    int frameRate() const;

    // 连接信息
    QString currentHost() const;
    int currentPort() const;
    bool isConnected() const;
    bool isAuthenticated() const;

    // Triple-buffered lock-free frame delivery
    TripleBuffer<FrameSlot>* frameBuffer() { return &m_frameBuffer; }

#ifndef QT_NO_OPENGL
    /// Initialize a shared OpenGL context for worker-thread texture upload.
    /// @param shareContext The GUI thread's GL context to share resources with.
    void initializeGLUpload(QOpenGLContext* shareContext);

    /// Set the GLTextureViewport reference for worker-side frame upload.
    void setGLViewportForUpload(GLTextureViewport* vp) { m_glViewportForUpload = vp; }
#endif

public slots:
    // 连接控制（声明为 slot 以支持跨线程调用）
    void connectToHost(const QString& host, int port);
    void disconnectFromHost();

    /// 重置当前连接状态：清理连接数据、重置内部状态、发出 connectionReset 信号。
    /// 线程安全，可从任意线程通过信号槽跨线程调用。
    void resetConnection();

signals:
    // 连接重置信号（在 resetConnection() 完成清理后发出）
    void connectionReset();

    // 远程桌面数据更新信号
    void screenUpdated(const QImage& screen);
    void screenRegionUpdated(const QImage& region, const QRect& rect);
    void performanceStatsUpdated(const PerformanceStats& stats);
    void sessionError(const QString& error);

    // 连接状态变化信号（用于 UI 更新）
    void connectionStateChanged(ConnectionManager::ConnectionState state);

    // 远程光标类型更新信号
    void remoteCursorTypeUpdated(Qt::CursorShape type);

    // 剪贴板数据接收信号
    void clipboardTextReceived(const QString& text);
    void clipboardImageReceived(const QByteArray& imageData);

private slots:
    void onMessageReceived(MessageType type, const QByteArray& data);
    void updatePerformanceStats();

private:
    void setupConnections();
    void calculateFPS();
    void handleScreenData(const QByteArray& data);
    void handleCursorPosition(const QByteArray& data);
    void handleClipboardData(const QByteArray& data);

    // 连接信息
    QString m_connectionId;
    ConnectionManager* m_connectionManager;

    // 远程桌面数据
    QSize m_remoteScreenSize;

    // 帧数据缓存和线程安全
    QByteArray m_previousFrameData;
    mutable QMutex m_frameDataMutex;

    // Triple-buffered lock-free frame delivery (replaces QQueue+QMutex+signal)
    TripleBuffer<FrameSlot> m_frameBuffer;

    // 性能统计
    QTimer* m_statsTimer;
    PerformanceStats m_stats;
    QQueue<QDateTime> m_frameTimes;

    // 配置
    int m_frameRate;

#ifndef QT_NO_OPENGL
    // Shared GL context for worker-thread texture upload
    QOpenGLContext* m_glContext = nullptr;
    QOffscreenSurface* m_glSurface = nullptr;
    bool m_glUploadReady = false;
    GLTextureViewport* m_glViewportForUpload = nullptr;
#endif
};

