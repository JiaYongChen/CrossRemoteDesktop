// src/server/session/ServerSession.cpp
#include "server/session/ServerSession.h"

#include <QtCore/QMetaObject>
#include <QtCore/QRandomGenerator>
#include <QtCore/QThread>
#include <QtCore/QUuid>
#include <QtNetwork/QPasswordDigestor>

#include "common/error/RdError.h"
#include "common/logging/LoggingCategories.h"
#include "common/threading/ThreadManager.h"
#include "server/clienthandler/ClientHandlerWorker.h"
#include "server/dataprocessing/DataProcessingWorker.h"
#include "server/capture/ScreenCaptureWorker.h"
#include "server/session/SessionQueuePair.h"

ServerSession::ServerSession(qintptr socketDescriptor,
                             const QSslCertificate& cert,
                             const QSslKey& key,
                             ThreadManager* threadMgr,
                             const QString& serverUsername,
                             const QString& serverPassword,
                             QObject* parent)
    : Worker(parent)
    , m_socketDescriptor(socketDescriptor)
    , m_cert(cert)
    , m_key(key)
    , m_threadManager(threadMgr)
    , m_serverUsername(serverUsername)
    , m_serverPassword(serverPassword) {
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

    // 2.5 如果配置了密码，每连接动态生成 salt 并派生 PBKDF2 摘要
    if (!m_serverPassword.isEmpty()) {
        // 生成随机 32 字节 salt
        QByteArray salt(32, Qt::Uninitialized);
        QRandomGenerator::securelySeeded().generate(salt.begin(), salt.end());

        // PBKDF2 派生: password + username + salt
        QByteArray derived = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha256,
            (m_serverPassword + m_serverUsername).toUtf8(),
            salt, 100000, 32);

        // 注入 AuthHandler
        m_clientHandler->setExpectedUsername(m_serverUsername);
        m_clientHandler->setExpectedPasswordDigest(salt, derived);
        m_clientHandler->setPbkdf2Params(100000, 32);

        qCDebug(lcServer) << "ServerSession: auth configured for" << m_sessionId;
    }

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

    // 6. 色深参数闭环：ClientHandlerWorker → DataProcessingWorker
    connect(m_clientHandler, &ClientHandlerWorker::colorDepthReceived,
            m_dataWorker, &DataProcessingWorker::setChromaSubsampling,
            Qt::QueuedConnection);

    // 7. 转发剪贴板数据
    connect(m_clientHandler, &ClientHandlerWorker::clipboardDataReceived,
            this, &ServerSession::clipboardDataReceived,
            Qt::QueuedConnection);

    qCInfo(lcServer) << "ServerSession initialized:" << m_sessionId;
    return true;
}

void ServerSession::cleanup() {
    qCDebug(lcServer) << "ServerSession::cleanup() —" << m_sessionId;

    // 逆序停止子线程
    // DataProc 线程为 autoStart=false，仅在认证成功后启动。认证失败即断开的
    // 会话其线程从未 exec()，此时 BlockingQueuedConnection 因目标线程无事件循环
    // 应答而永久阻塞，导致 cleanup() 卡死、disconnected 信号不发射、会话残留。
    if (m_dataWorker && m_dataWorker->thread()
        && m_dataWorker->thread()->isRunning()) {
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

void ServerSession::sendClipboardData(const QByteArray& encodedMessage) {
    if (m_shuttingDown || !m_clientHandler) return;
    QMetaObject::invokeMethod(m_clientHandler,
                              &ClientHandlerWorker::sendEncodedMessage,
                              Qt::QueuedConnection,
                              encodedMessage);
}
