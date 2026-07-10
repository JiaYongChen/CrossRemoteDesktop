// src/server/service/ServerService.h
#pragma once

#include <QObject>
#include <QList>
#include "error/RdError.h"

class ThreadManager;
class QueueManager;
class TcpListener;
class CapturePipeline;
class ServerSession;
struct RdError;

/**
 * @brief 服务端编排门面 — 管理 TcpListener + CapturePipeline + 会话生命周期
 *
 * 从 MainWindow 拆出，使服务端启动/停止/会话管理可脱离 UI 独立运作和测试。
 */
class ServerService : public QObject {
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Starting,
        Listening,
        Stopping
    };
    Q_ENUM(State)

    explicit ServerService(ThreadManager *threadManager,
                           QueueManager *queueManager,
                           QObject *parent = nullptr);
    ~ServerService() override;

    bool start(quint16 port);
    void stop();
    bool isRunning() const;
    int clientCount() const;
    quint16 port() const;

signals:
    void stateChanged(ServerService::State newState);
    void listening(quint16 port);
    void stopped();
    void clientConnected(const QString &sessionId);
    void clientDisconnected(const QString &sessionId);
    void clientAuthenticated(const QString &sessionId);
    void errorOccurred(const RdError &error);

private slots:
    void onTcpListenerListening(quint16 port);
    void onTcpListenerStopped();
    void onTcpListenerError(const RdError &error);
    void onNewConnection(qintptr socketDescriptor);
    void onSessionAuthenticated(const QString &sessionId);
    void onSessionDisconnected(const QString &sessionId);

private:
    void startCapturePipeline();
    void stopCapturePipeline();
    void cleanupSessions();

    ThreadManager *m_threadManager;
    QueueManager *m_queueManager;

    TcpListener *m_tcpListener = nullptr;
    CapturePipeline *m_capturePipeline = nullptr;
    QList<ServerSession *> m_sessions;

    State m_state = State::Stopped;
    quint16 m_port = 0;
};
