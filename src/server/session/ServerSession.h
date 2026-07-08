// src/server/session/ServerSession.h
#pragma once

#include "../../common/core/threading/Worker.h"

#include <QtCore/QObject>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslKey>
#include <memory>

class ThreadManager;
class DataProcessingWorker;
class ClientHandlerWorker;
struct SessionQueuePair;
struct CapturedFrame;

/**
 * @brief 服务端会话 — 在独立线程中管理单个客户端连接的完整生命周期
 *
 * 继承 Worker。与客户端 RemoteDesktopSession 对称。
 * initialize() 创建 SessionQueuePair + DataProcessingWorker + ClientHandlerWorker。
 * enqueueFrame() 由 FrameBroadcaster 跨线程调用，入队到私有捕获队列。
 * shutdown() 由 MainWindow 跨线程调用，逆序停止子线程并清理。
 */
class ServerSession : public Worker {
    Q_OBJECT

public:
    explicit ServerSession(qintptr socketDescriptor,
                           const QSslCertificate& cert,
                           const QSslKey& key,
                           ThreadManager* threadMgr,
                           QObject* parent = nullptr);
    ~ServerSession() override;

    // ── Q_INVOKABLE（跨线程调用）──
    Q_INVOKABLE void enqueueFrame(const CapturedFrame& frame);
    Q_INVOKABLE void shutdown();

    // ── 查询 ──
    QString sessionId() const;
    QString clientAddress() const;
    bool isAuthenticated() const;

signals:
    void authenticated(const QString& sessionId);
    void disconnected(const QString& sessionId);
    // 注意：errorOccurred 信号由 Worker 基类声明，此处不重复声明

protected:
    bool initialize() override;
    Q_INVOKABLE void cleanup() override;
    void processTask() override;

private:
    const qintptr m_socketDescriptor;
    QSslCertificate m_cert;
    QSslKey m_key;
    ThreadManager* m_threadManager;

    std::unique_ptr<SessionQueuePair> m_queues;
    DataProcessingWorker* m_dataWorker = nullptr;
    ClientHandlerWorker* m_clientHandler = nullptr;
    QString m_clientHandlerThreadName;

    QString m_sessionId;
    bool m_authenticated = false;
    bool m_shuttingDown = false;
};
