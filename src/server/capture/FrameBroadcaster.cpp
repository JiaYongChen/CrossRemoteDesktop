// src/server/capture/FrameBroadcaster.cpp
#include "FrameBroadcaster.h"
#include "../session/ServerSession.h"
#include "../dataflow/QueueManager.h"
#include "../dataflow/DataFlowStructures.h"
#include "../../common/logging/LoggingCategories.h"

#include <QtCore/QMetaObject>

FrameBroadcaster::FrameBroadcaster(QueueManager* queueMgr, QObject* parent)
    : QObject(parent)
    , m_queueManager(queueMgr) {
}

void FrameBroadcaster::start() {
    m_active = true;
    qCDebug(lcServerCapture) << "FrameBroadcaster started";
}

void FrameBroadcaster::stop() {
    m_active = false;
    qCDebug(lcServerCapture) << "FrameBroadcaster stopped";
}

void FrameBroadcaster::addSubscriber(ServerSession* session) {
    if (session && !m_subscribers.contains(session)) {
        m_subscribers.insert(session);
        qCDebug(lcServerCapture) << "FrameBroadcaster: subscriber added, total:" << m_subscribers.size();
    }
}

void FrameBroadcaster::removeSubscriber(ServerSession* session) {
    if (m_subscribers.remove(session)) {
        qCDebug(lcServerCapture) << "FrameBroadcaster: subscriber removed, total:" << m_subscribers.size();
    }
}

void FrameBroadcaster::onFrameReady() {
    if (!m_active || m_subscribers.isEmpty()) return;

    CapturedFrame frame;
    if (!m_queueManager->dequeueCapturedFrame(frame) || !frame.isValid()) return;

    broadcastFrame(frame);
}

void FrameBroadcaster::broadcastFrame(const CapturedFrame& frame) {
    for (auto* session : m_subscribers) {
        if (!session) continue;
        // frame 通过 Q_ARG 拷贝到 session 线程（CapturedFrame::image 是 shared_ptr，浅拷贝）
        QMetaObject::invokeMethod(session, "enqueueFrame", Qt::QueuedConnection,
                                  Q_ARG(CapturedFrame, frame));
    }
}
