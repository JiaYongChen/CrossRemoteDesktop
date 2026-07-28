// src/server/session/ServerSession.h
#pragma once

#include <QtCore/QObject>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslKey>

#include <atomic>
#include <memory>

#include "common/threading/Worker.h"
#include "SessionQueuePair.h"

class ClientHandlerWorker;
class DataProcessingWorker;
class ScreenCaptureWorker;
class ThreadManager;
struct ClipboardMessage;

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
                           const QString& serverUsername,
                           const QString& serverPassword,
                           QObject* parent = nullptr);
    ~ServerSession() override;

    // ── Q_INVOKABLE（跨线程调用）──
    Q_INVOKABLE void enqueueFrame(const CapturedFrame& frame);
    Q_INVOKABLE void wireCursorUpdates(ScreenCaptureWorker* worker);
    Q_INVOKABLE void shutdown();

    /**
     * @brief 发送剪贴板消息到客户端（跨线程安全）
     * @param encodedMessage 已编码的消息包（含消息头和载荷）
     */
    Q_INVOKABLE void sendClipboardData(const QByteArray& encodedMessage);

    // ── 查询 ──
    QString sessionId() const;
    QString clientAddress() const;
    bool isAuthenticated() const;
    qintptr socketDescriptor() const { return m_socketDescriptor; }

signals:
    void authenticated(const QString& sessionId);
    void disconnected(const QString& sessionId);
    /**
     * @brief 转发客户端剪贴板数据
     * @param message 剪贴板消息
     */
    void clipboardDataReceived(const ClipboardMessage& message);
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
    QString m_serverUsername;
    QString m_serverPassword;
    std::atomic<bool> m_authenticated{false};
    bool m_shuttingDown = false;
};
