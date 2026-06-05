#pragma once

#include <QtCore/QObject>
#include <QtGui/QImage>
#include <QtCore/QDateTime>
#include <QtCore/QSize>
#include "../../common/core/network/Protocol.h"
#include "../../common/core/config/UiConstants.h"
#include "../network/ConnectionManager.h"
#include "../core/TripleBuffer.h"
#include "../core/FrameSlot.h"
#include <chrono>

class DecodeWorker;

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


public:
    // 性能统计
    PerformanceStats performanceStats() const;
    void resetStats();

    // 性能信息格式化
    QString getFormattedPerformanceInfo() const;

    // 连接信息
    QString currentHost() const;
    int currentPort() const;
    bool isConnected() const;
    bool isAuthenticated() const;

    // Triple-buffered lock-free frame delivery
    TripleBuffer<FrameSlot>* frameBuffer() { return &m_frameBuffer; }

    /// 创建并启动解码管线（在 startSession 中调用，认证成功后延迟创建）
    void createDecodePipeline();

    /// 停止并销毁解码管线（在关闭连接时先于 disconnectFromHost 调用）
    void destroyDecodePipeline();

#ifndef QT_NO_OPENGL
    /// 设置解码管线使用的 GL 上下文（在 DecodeWorker 创建时使用）
    void setGLContextForDecode(QOpenGLContext* context);

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

    // Triple-buffered lock-free frame delivery (replaces QQueue+QMutex+signal)
    TripleBuffer<FrameSlot> m_frameBuffer;

    // 解码管线（认证成功后创建）
    DecodeWorker* m_decodeWorker = nullptr;

    // 性能统计
    QTimer* m_statsTimer;
    PerformanceStats m_stats;

    // EMA 指数滑动平均 FPS 追踪（替代 QQueue<QDateTime>，消除 STL Debug 迭代器开销）
    std::chrono::steady_clock::time_point m_lastFpsTime{};
    double m_smoothedFrameDuration = 0.0;  // EMA 平滑帧间隔（秒）
    static constexpr double kFpsAlpha = 0.1;     // EMA 平滑系数

#ifndef QT_NO_OPENGL
    GLTextureViewport* m_glViewportForUpload = nullptr;
    QOpenGLContext* m_pendingGLContext = nullptr;
#endif
};

