// src/server/session/ServerSession.cpp
#include "ServerSession.h"
#include "SessionQueuePair.h"
#include "../dataprocessing/DataProcessingWorker.h"
#include "../clienthandler/ClientHandlerWorker.h"
#include "../capture/ScreenCaptureWorker.h"
#include "../../common/core/threading/ThreadManager.h"
#include "../../common/core/logging/LoggingCategories.h"

#include <QtCore/QMetaObject>
#include <QtCore/QThread>
#include <QtCore/QUuid>

ServerSession::ServerSession(qintptr socketDescriptor,
                             const QSslCertificate& cert,
                             const QSslKey& key,
                             ThreadManager* threadMgr,
                             QObject* parent)
    : Worker(parent)
    , m_socketDescriptor(socketDescriptor)
    , m_cert(cert)
    , m_key(key)
    , m_threadManager(threadMgr) {
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setName(QString("ServerSession_%1").arg(m_sessionId.left(8)));
}

ServerSession::~ServerSession() {
    if (!m_shuttingDown) {
        shutdown();
    }
}

bool ServerSession::initialize() {
    qCDebug(lcServer) << "ServerSession::initialize() —" << m_sessionId;

    // 1. 创建私有队列对
    m_queues = std::make_unique<SessionQueuePair>();
    m_queues->initialize();

    // 2. 创建 ClientHandlerWorker（子线程）
    m_clientHandler = new ClientHandlerWorker(m_socketDescriptor,
                                              &m_queues->processedQueue,
                                              m_cert, m_key);

    m_clientHandlerThreadName = QString("ClientHandler_%1").arg(m_sessionId.left(8));
    if (!m_threadManager->createThread(m_clientHandlerThreadName,
                                       std::unique_ptr<Worker>(m_clientHandler), true)) {
        qCCritical(lcServer) << "ServerSession: Failed to create ClientHandlerWorker thread";
        emit errorOccurred(RdError(ErrorCode::ThreadStartFailed,
                                   QStringLiteral("ClientHandlerWorker 线程创建失败"),
                                   "ServerSession"));
        return false;
    }

    // 3. 创建 DataProcessingWorker（子线程）
    m_dataWorker = new DataProcessingWorker();
    m_dataWorker->setQueues(&m_queues->captureQueue, &m_queues->processedQueue);
    m_dataWorker->setProcessingTimeout(2000);

    const QString dataThreadName = QString("DataProc_%1").arg(m_sessionId.left(8));
    if (!m_threadManager->createThread(dataThreadName,
                                       std::unique_ptr<Worker>(m_dataWorker), false, true, 3)) {
        qCCritical(lcServer) << "ServerSession: Failed to create DataProcessingWorker thread";
        emit errorOccurred(RdError(ErrorCode::ThreadStartFailed,
                                   QStringLiteral("DataProcessingWorker 线程创建失败"),
                                   "ServerSession"));
        return false;
    }

    // 4. 连接 ClientHandlerWorker 信号
    connect(m_clientHandler, &ClientHandlerWorker::authenticated, this, [this, dataThreadName]() {
        m_authenticated = true;
        // 启动 DataProcessingWorker
        (void)m_threadManager->startThread(dataThreadName);
        QMetaObject::invokeMethod(m_dataWorker, "resumeProcessing", Qt::QueuedConnection);
        emit authenticated(m_sessionId);
        qCInfo(lcServer) << "ServerSession authenticated:" << m_sessionId;
    }, Qt::QueuedConnection);

    connect(m_clientHandler, &ClientHandlerWorker::disconnected, this, [this]() {
        qCInfo(lcServer) << "ServerSession client disconnected:" << m_sessionId;
        shutdown();
    }, Qt::QueuedConnection);

    connect(m_clientHandler, &ClientHandlerWorker::errorOccurred, this, [this](const RdError& err) {
        qCWarning(lcServer) << "ServerSession client error:" << err.logLabel();
        emit errorOccurred(err);
    }, Qt::QueuedConnection);

    // 5. 质量参数闭环：ClientHandlerWorker → DataProcessingWorker
    connect(m_clientHandler, &ClientHandlerWorker::qualitySettingsReceived,
            m_dataWorker, &DataProcessingWorker::setJpegQuality,
            Qt::QueuedConnection);

    qCInfo(lcServer) << "ServerSession initialized:" << m_sessionId;
    return true;
}

void ServerSession::cleanup() {
    qCDebug(lcServer) << "ServerSession::cleanup() —" << m_sessionId;

    // 逆序停止子线程
    if (m_dataWorker) {
        QMetaObject::invokeMethod(m_dataWorker, "stopProcessingAndClearQueues",
                                  Qt::BlockingQueuedConnection);
    }

    if (!m_clientHandlerThreadName.isEmpty() && m_threadManager) {
        (void)m_threadManager->stopThread(m_clientHandlerThreadName, false);
        (void)m_threadManager->destroyThread(m_clientHandlerThreadName);
    }

    // DataProcessingWorker 线程
    const QString dataThreadName = QString("DataProc_%1").arg(m_sessionId.left(8));
    if (m_threadManager && m_threadManager->hasThread(dataThreadName)) {
        (void)m_threadManager->stopThread(dataThreadName, false);
        (void)m_threadManager->destroyThread(dataThreadName);
    }

    // 清理队列
    if (m_queues) {
        m_queues->captureQueue.clear();
        m_queues->processedQueue.clear();
    }

    m_dataWorker = nullptr;
    m_clientHandler = nullptr;

    qCInfo(lcServer) << "ServerSession cleaned up:" << m_sessionId;
}

void ServerSession::processTask() {
    QThread::msleep(1);  // 事件循环驱动
}

void ServerSession::enqueueFrame(const CapturedFrame& frame) {
    if (m_shuttingDown || !m_queues) return;
    m_queues->captureQueue.tryEnqueueDrainToLatest(frame);  // Drain-to-Latest
}

void ServerSession::wireCursorUpdates(ScreenCaptureWorker* worker) {
    if (m_clientHandler && worker) {
        m_clientHandler->setScreenCaptureWorker(worker);
    }
}

void ServerSession::shutdown() {
    if (m_shuttingDown) return;
    m_shuttingDown = true;

    qCInfo(lcServer) << "ServerSession::shutdown() —" << m_sessionId;

    cleanup();
    emit disconnected(m_sessionId);
}

QString ServerSession::sessionId() const {
    return m_sessionId;
}

QString ServerSession::clientAddress() const {
    if (m_clientHandler) {
        return m_clientHandler->clientAddress();
    }
    return QString();
}

bool ServerSession::isAuthenticated() const {
    return m_authenticated;
}
