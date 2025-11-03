#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

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

class QTimer;

class SessionManager : public QObject {
    Q_OBJECT

public:
    struct PerformanceStats {
        double currentFPS;
        QDateTime sessionStartTime;
        int frameCount;
    };

    explicit SessionManager(QObject* parent = nullptr);
    ~SessionManager();

    // 会话控制
    void startSession();
    void suspendSession();
    void resumeSession();
    void terminateSession();

    // 状态查询
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

    // 连接信息
    QString currentHost() const;
    int currentPort() const;
    bool isConnected() const;
    bool isAuthenticated() const;

    // 连接控制
    void connectToHost(const QString& host, int port);
    void disconnectFromHost();

signals:
    void screenUpdated(const QPixmap& screen);
    void screenRegionUpdated(const QPixmap& region, const QRect& rect);
    void performanceStatsUpdated(const PerformanceStats& stats);
    void sessionError(const QString& error);
    
    // 连接状态变化信号（用于 UI 更新）
    void connectionStateChanged(ConnectionManager::ConnectionState state);

private slots:
    void onMessageReceived(MessageType type, const QByteArray& data);
    void updatePerformanceStats();

private:
    void setupConnections();
    void calculateFPS();
    void handleScreenData(const QByteArray& data);

    ConnectionManager* m_connectionManager;

    // 远程桌面数据
    QPixmap m_currentScreen;
    QSize m_remoteScreenSize;

    // 帧数据缓存和线程安全
    QByteArray m_previousFrameData;
    mutable QMutex* m_frameDataMutex;

    // 性能统计
    QTimer* m_statsTimer;
    PerformanceStats m_stats;
    QQueue<QDateTime> m_frameTimes;

    // 配置
    int m_frameRate;
};

#endif // SESSIONMANAGER_H