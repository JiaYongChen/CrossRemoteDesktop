// src/server/capture/CapturePipeline.h
#pragma once

#include <QtCore/QObject>

#include "common/threading/Worker.h"

class FrameBroadcaster;
class QueueManager;
class ScreenCapture;
class ServerSession;
class ThreadManager;

/**
 * @brief 捕获管线 — 在独立线程中管理屏幕捕获 + 帧广播
 *
 * 继承 Worker。initialize() 创建 ScreenCapture 和 FrameBroadcaster，
 * 并将 ScreenCaptureWorker::frameEnqueued 连接到 FrameBroadcaster::onFrameReady。
 * 订阅管理通过 QMetaObject::invokeMethod 转发到 FrameBroadcaster 线程。
 */
class CapturePipeline : public Worker {
    Q_OBJECT

public:
    explicit CapturePipeline(ThreadManager* threadMgr,
                             QueueManager* queueMgr,
                             QObject* parent = nullptr);

    // 订阅管理（Main 线程调用 → QMetaObject::invokeMethod 到 FrameBroadcaster）
    Q_INVOKABLE void subscribe(ServerSession* session);
    Q_INVOKABLE void unsubscribe(ServerSession* session);

    // 捕获控制
    Q_INVOKABLE void startCapture();
    Q_INVOKABLE void stopCapture();
    bool isCapturing() const;

signals:
    // 注意：errorOccurred 继承自 Worker 基类，不在此处重复声明

protected:
    bool initialize() override;
    Q_INVOKABLE void cleanup() override;
    void processTask() override;  // return（事件循环驱动）

private:
    ThreadManager* m_threadManager;
    QueueManager* m_queueManager;
    ScreenCapture* m_screenCapture = nullptr;
    FrameBroadcaster* m_broadcaster = nullptr;
    bool m_captureActive = false;
    QMetaObject::Connection m_frameConnection;  // frameEnqueued → onFrameReady 连接
};
