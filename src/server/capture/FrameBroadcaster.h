// src/server/capture/FrameBroadcaster.h
#pragma once

#include <QtCore/QObject>
#include <QtCore/QSet>

class QueueManager;
class ServerSession;
struct CapturedFrame;

/**
 * @brief 帧广播器 — 从共享捕获队列拉帧，广播到所有订阅 session
 *
 * 归属 CapturePipeline 线程。通过 QueuedConnection 接收
 * ScreenCaptureWorker::frameEnqueued 信号，dequeue 最新帧后
 * 通过 QMetaObject::invokeMethod 跨线程投递到各 ServerSession。
 */
class FrameBroadcaster : public QObject {
    Q_OBJECT

public:
    explicit FrameBroadcaster(QueueManager* queueMgr, QObject* parent = nullptr);

    bool isActive() const { return m_active; }

public slots:
    void addSubscriber(ServerSession* session);
    void removeSubscriber(ServerSession* session);
    void start();
    void stop();

public slots:
    void onFrameReady();

private:
    void broadcastFrame(const CapturedFrame& frame);

    QueueManager* m_queueManager;
    QSet<ServerSession*> m_subscribers;
    bool m_active = false;
};
