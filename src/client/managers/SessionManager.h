#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QtCore/QObject>
#include <QtGui/QPixmap>
#include <QtGui/QImage>
#include <QtCore/QDateTime>
#include <QtCore/QQueue>
#include <QtCore/QSize>
#include "../../common/core/network/Protocol.h"
#include "../../common/core/config/UiConstants.h"

class TcpClient;
class ConnectionManager;
class QTimer;

class SessionManager : public QObject {
    Q_OBJECT

public:
    enum SessionState {
        Inactive,
        Initializing,
        Active,
        Suspended,
        Terminated
    };
    Q_ENUM(SessionState)

        struct PerformanceStats {
        double currentFPS;
        QDateTime sessionStartTime;
        int frameCount;
    };

    explicit SessionManager(ConnectionManager* connectionManager, QObject* parent = nullptr);
    ~SessionManager();

    // 会话控制
    void startSession();
    void suspendSession();
    void resumeSession();
    void terminateSession();

    // 状态查询
    SessionState sessionState() const;
    bool isActive() const;

    // 远程桌面数据
    QPixmap currentScreen() const;
    QSize remoteScreenSize() const;

    // 输入事件发送
    void sendMouseEvent(int x, int y, int buttons, int eventType);
    void sendKeyboardEvent(int key, int modifiers, bool pressed, const QString& text);
    void sendWheelEvent(int x, int y, int delta, int orientation);

    // 性能统计
    PerformanceStats performanceStats() const;
    void resetStats();

    // 性能信息格式化
    QString getFormattedPerformanceInfo() const;

    // 配置
    void setFrameRate(int fps);
    int frameRate() const;

signals:
    void sessionStateChanged(SessionState state);
    void screenUpdated(const QPixmap& screen);
    void screenRegionUpdated(const QPixmap& region, const QRect& rect);
    void performanceStatsUpdated(const PerformanceStats& stats);
    void sessionError(const QString& error);
    void connectionStateChanged(int state);

private slots:
    void onConnectionStateChanged();
    void onScreenDataReceived(const QImage& image);
    void onMessageReceived(MessageType type, const QByteArray& data);
    void updatePerformanceStats();

private:
    void setSessionState(SessionState state);
    void setupConnections();
    void processInputResponse(const QByteArray& data);
    void calculateFPS();

    ConnectionManager* m_connectionManager;
    TcpClient* m_tcpClient;
    SessionState m_sessionState;

    // 远程桌面数据
    QPixmap m_currentScreen;
    QSize m_remoteScreenSize;

    // 性能统计
    QTimer* m_statsTimer;
    PerformanceStats m_stats;
    QQueue<QDateTime> m_frameTimes;

    // 配置
    int m_frameRate;
};

#endif // SESSIONMANAGER_H