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
#include <atomic>

class QTimer;

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

    // 图片队列操作（线程安全）
    bool hasScreenImage() const;
    QImage dequeueScreenImage();

    /**
     * @brief Reset the frame notification coalescing flag.
     *
     * Must be called by the consumer (ClientManager) after it has drained
     * or attempted to drain the queue, so that the next enqueue can
     * re-emit frameAvailable().
     */
    void resetFrameNotification();

public slots:
    // 连接控制（声明为 slot 以支持跨线程调用）
    void connectToHost(const QString& host, int port);
    void disconnectFromHost();

signals:
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

    /**
     * @brief Lightweight notification that a new frame is available in the queue.
     *
     * Uses atomic flag coalescing: only emitted when the flag transitions
     * from false to true, preventing signal storms under high frame rates.
     * The consumer must call resetFrameNotification() after draining the queue.
     */
    void frameAvailable();

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

    // 图片队列（用于替代信号槽机制）
    QQueue<QImage> m_screenImageQueue;
    mutable QMutex m_screenImageQueueMutex;
    static constexpr int MAX_QUEUE_SIZE = 5;  // Queue capacity (absorb network jitter)

    // Coalescing flag for frameAvailable() signal: prevents signal storms
    // when frames arrive faster than the consumer can process them.
    std::atomic<bool> m_frameNotificationPending{false};

    // 性能统计
    QTimer* m_statsTimer;
    PerformanceStats m_stats;
    QQueue<QDateTime> m_frameTimes;

    // 配置
    int m_frameRate;
};

