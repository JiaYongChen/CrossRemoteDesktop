// src/server/capture/CapturePipeline.cpp
#include "server/capture/CapturePipeline.h"

#include <QtCore/QMetaObject>
#include <QtCore/QThread>

#include "common/logging/LoggingCategories.h"
#include "common/threading/ThreadManager.h"
#include "server/capture/FrameBroadcaster.h"
#include "server/capture/ScreenCapture.h"
#include "server/capture/ScreenCaptureWorker.h"
#include "server/dataflow/QueueManager.h"
#include "server/session/ServerSession.h"

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

    // 必须先启动屏幕捕获（内部创建 ScreenCaptureWorker），再获取 worker 建立信号连接。
    // 修复：原来的 getWorker() 调用在 startCapture() 之前，此时 worker 尚未创建，
    // 导致 frameEnqueued → onFrameReady 连接从未建立，帧数据无法广播到客户端（黑屏）。
    m_screenCapture->startCapture();
    m_broadcaster->start();

    // 获取 ScreenCaptureWorker 并建立帧广播管线
    auto* captureWorker = qobject_cast<ScreenCaptureWorker*>(
        m_threadManager->getWorker(QStringLiteral("ScreenCaptureWorker")));
    if (captureWorker) {
        // 帧入队信号 → 帧广播器：此为数据管线的核心桥接
        m_frameConnection = connect(captureWorker, &ScreenCaptureWorker::frameEnqueued,
                                    m_broadcaster, &FrameBroadcaster::onFrameReady,
                                    Qt::QueuedConnection);

        // 桥接光标更新信号到所有已订阅 session
        for (auto* session : m_broadcaster->subscribers()) {
            QMetaObject::invokeMethod(session, "wireCursorUpdates", Qt::QueuedConnection,
                                      Q_ARG(ScreenCaptureWorker*, captureWorker));
        }
    } else {
        qCWarning(lcServerCapture) << "CapturePipeline::startCapture() - "
                                      "无法获取 ScreenCaptureWorker，帧数据将无法广播到客户端";
    }

    m_captureActive = true;
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
}

bool CapturePipeline::isCapturing() const {
    return m_captureActive;
}
