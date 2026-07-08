// src/server/capture/CapturePipeline.cpp
#include "CapturePipeline.h"
#include "FrameBroadcaster.h"
#include "ScreenCapture.h"
#include "ScreenCaptureWorker.h"
#include "../session/ServerSession.h"
#include "../dataflow/QueueManager.h"
#include "../../common/core/threading/ThreadManager.h"
#include "../../common/core/logging/LoggingCategories.h"

#include <QtCore/QMetaObject>
#include <QtCore/QThread>

CapturePipeline::CapturePipeline(ThreadManager* threadMgr,
                                 QueueManager* queueMgr,
                                 QObject* parent)
    : Worker(parent)
    , m_threadManager(threadMgr)
    , m_queueManager(queueMgr) {
    setName("CapturePipeline");
}

bool CapturePipeline::initialize() {
    qCDebug(lcServerCapture) << "CapturePipeline::initialize()";

    // 创建屏幕捕获管理器
    m_screenCapture = new ScreenCapture(m_threadManager, m_queueManager, this);

    // 创建帧广播器
    m_broadcaster = new FrameBroadcaster(m_queueManager, this);

    qCInfo(lcServerCapture) << "CapturePipeline initialized";
    return true;
}

void CapturePipeline::cleanup() {
    qCDebug(lcServerCapture) << "CapturePipeline::cleanup()";

    if (m_captureActive) {
        stopCapture();
    }
    // m_screenCapture 和 m_broadcaster 由 Qt parent 自动清理
    qCInfo(lcServerCapture) << "CapturePipeline cleaned up";
}

void CapturePipeline::processTask() {
    // 事件循环驱动，不做轮询
    QThread::msleep(1);
}

void CapturePipeline::subscribe(ServerSession* session) {
    if (!m_broadcaster || !session) return;
    QMetaObject::invokeMethod(m_broadcaster, "addSubscriber", Qt::QueuedConnection,
                              Q_ARG(ServerSession*, session));
    qCDebug(lcServerCapture) << "CapturePipeline::subscribe() — session:" << session;
}

void CapturePipeline::unsubscribe(ServerSession* session) {
    if (!m_broadcaster || !session) return;
    QMetaObject::invokeMethod(m_broadcaster, "removeSubscriber", Qt::QueuedConnection,
                              Q_ARG(ServerSession*, session));
    qCDebug(lcServerCapture) << "CapturePipeline::unsubscribe() — session:" << session;
}

void CapturePipeline::startCapture() {
    if (m_captureActive) return;
    if (!m_screenCapture || !m_broadcaster) return;

    qCInfo(lcServerCapture) << "CapturePipeline::startCapture()";

    // 连接 ScreenCaptureWorker::frameEnqueued → FrameBroadcaster::onFrameReady
    // 通过 ScreenCapture 内部获取 worker 连接（或通过 ThreadManager 获取 ScreenCaptureWorker）
    auto* captureWorker = qobject_cast<ScreenCaptureWorker*>(
        m_threadManager->getWorker(QStringLiteral("ScreenCaptureWorker")));
    if (captureWorker) {
        m_frameConnection = connect(captureWorker, &ScreenCaptureWorker::frameEnqueued,
                                    m_broadcaster, &FrameBroadcaster::onFrameReady,
                                    Qt::QueuedConnection);
    }

    m_screenCapture->startCapture();
    m_broadcaster->start();
    m_captureActive = true;
    emit captureStarted();
}

void CapturePipeline::stopCapture() {
    if (!m_captureActive) return;

    qCInfo(lcServerCapture) << "CapturePipeline::stopCapture()";

    m_broadcaster->stop();
    disconnect(m_frameConnection);

    if (m_screenCapture) {
        m_screenCapture->stopCapture();
    }

    m_captureActive = false;
    emit captureStopped();
}

bool CapturePipeline::isCapturing() const {
    return m_captureActive;
}
