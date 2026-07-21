// src/server/service/ServerService.cpp
#include "ServerService.h"
#include "../listener/TcpListener.h"
#include "../capture/CapturePipeline.h"
#include "../session/ServerSession.h"
#include "../../common/threading/ThreadManager.h"
#include "../dataflow/QueueManager.h"
#include "../../common/logging/LoggingCategories.h"
#include <QtCore/QTimer>

ServerService::ServerService(ThreadManager *threadManager,
                             QueueManager *queueManager,
                             QObject *parent)
    : QObject(parent)
    , m_threadManager(threadManager)
    , m_queueManager(queueManager)
{
}

ServerService::~ServerService()
{
    stop();
}

bool ServerService::start(quint16 port)
{
    if (m_state != State::Stopped) {
        qCWarning(lcServer) << "ServerService: already running";
        return false;
    }

    m_port = port;
    m_state = State::Starting;

    // 1. 创建 TcpListener
    if (!m_threadManager->hasThread("TcpListener")) {
        auto listener = std::make_unique<TcpListener>();
        m_tcpListener = listener.get();
        if (!m_threadManager->createThread("TcpListener", std::move(listener))) {
            qCCritical(lcServer) << "ServerService: failed to create TcpListener thread";
            m_tcpListener = nullptr;
            m_state = State::Stopped;
            return false;
        }
    }

    // 断开旧连接避免 stop/start 重复调用时信号累积
    disconnect(m_tcpListener, nullptr, this, nullptr);

    // 连接信号（必须在 startThread 前连接以避免 Worker::started 竞态）
    connect(m_tcpListener, &TcpListener::listening,
            this, &ServerService::onTcpListenerListening);
    connect(m_tcpListener, &TcpListener::stopped,
            this, &ServerService::onTcpListenerStopped);
    connect(m_tcpListener, &TcpListener::errorOccurred,
            this, &ServerService::onTcpListenerError);
    connect(m_tcpListener, &TcpListener::newConnection,
            this, &ServerService::onNewConnection);

    // Worker::started 连接必须在 startThread 之前（修复竞态窗口）
    connect(m_tcpListener, &Worker::started, this, [this]() {
        QMetaObject::invokeMethod(m_tcpListener, "startListening", Qt::QueuedConnection,
                                  Q_ARG(quint16, m_port), Q_ARG(QString, QString()));
    }, Qt::SingleShotConnection);

    if (!m_threadManager->startThread("TcpListener")) {
        qCCritical(lcServer) << "ServerService: failed to start TcpListener thread";
        m_state = State::Stopped;
        return false;
    }

    // 2. 创建 CapturePipeline
    startCapturePipeline();

    return true;
}

void ServerService::stop()
{
    if (m_state == State::Stopped) return;

    m_state = State::Stopping;

    cleanupSessions();
    stopCapturePipeline();

    if (m_tcpListener) {
        QMetaObject::invokeMethod(m_tcpListener, "stopListening", Qt::QueuedConnection);
        // 等待 TcpListener 线程停止后再由 onTcpListenerStopped 设置 Stopped 状态
        static_cast<void>(m_threadManager->stopThread("TcpListener", false));
    } else {
        // 无 TcpListener 时直接进入 Stopped
        m_state = State::Stopped;
    }
}

bool ServerService::isRunning() const
{
    return m_state == State::Listening;
}

quint16 ServerService::port() const
{
    return m_port;
}

void ServerService::startCapturePipeline()
{
    if (!m_threadManager->hasThread("CapturePipeline")) {
        auto pipeline = std::make_unique<CapturePipeline>(m_threadManager, m_queueManager);
        m_capturePipeline = pipeline.get();
        if (!m_threadManager->createThread("CapturePipeline", std::move(pipeline))) {
            qCCritical(lcServer) << "ServerService: failed to create CapturePipeline";
            m_capturePipeline = nullptr;
            return;
        }
    }

    // 转发 CapturePipeline 错误信号（先断开避免 stop/start 循环累积）
    disconnect(m_capturePipeline, &Worker::errorOccurred,
               this, &ServerService::errorOccurred);
    connect(m_capturePipeline, &Worker::errorOccurred,
            this, &ServerService::errorOccurred);

    if (!m_threadManager->startThread("CapturePipeline")) {
        qCWarning(lcServer) << "ServerService: failed to start CapturePipeline";
    }
}

void ServerService::stopCapturePipeline()
{
    if (m_capturePipeline) {
        QMetaObject::invokeMethod(m_capturePipeline, "stopCapture", Qt::QueuedConnection);
        static_cast<void>(m_threadManager->stopThread("CapturePipeline", false));
    }
}

void ServerService::cleanupSessions()
{
    for (auto *session : m_sessions) {
        // 断开信号避免 shutdown 后 stale 回调
        disconnect(session, nullptr, this, nullptr);
        QMetaObject::invokeMethod(session, "shutdown", Qt::QueuedConnection);
    }
    m_sessions.clear();
}

void ServerService::onTcpListenerListening(quint16 port)
{
    Q_UNUSED(port);
    m_state = State::Listening;
}

void ServerService::onTcpListenerStopped()
{
    m_state = State::Stopped;
}

void ServerService::onTcpListenerError(const RdError &error)
{
    emit errorOccurred(error);
}

void ServerService::onNewConnection(qintptr socketDescriptor)
{
    qCInfo(lcServer) << "ServerService: new connection, descriptor:" << socketDescriptor;

    auto cert = m_tcpListener->sslCertificate();
    auto key = m_tcpListener->sslPrivateKey();

    auto session = std::make_unique<ServerSession>(socketDescriptor, cert, key,
                                                    m_threadManager);
    auto *sessionPtr = session.get();

    QString threadName = QString("ServerSession_%1").arg(socketDescriptor);
    if (!m_threadManager->createThread(threadName, std::move(session), true)) {
        qCCritical(lcServer) << "ServerService: failed to create ServerSession thread";
        return;
    }

    connect(sessionPtr, &ServerSession::authenticated,
            this, &ServerService::onSessionAuthenticated);
    connect(sessionPtr, &ServerSession::disconnected,
            this, &ServerService::onSessionDisconnected);
    connect(sessionPtr, &ServerSession::errorOccurred,
            this, [this](const RdError &err) {
                qCWarning(lcServer) << "ServerSession error:" << err.logLabel();
            });

    m_sessions.append(sessionPtr);

    if (m_capturePipeline) {
        QMetaObject::invokeMethod(m_capturePipeline, "subscribe", Qt::QueuedConnection,
                                  Q_ARG(ServerSession *, sessionPtr));
    }

    emit clientConnected(QString::number(socketDescriptor));
}

void ServerService::onSessionAuthenticated(const QString &sessionId)
{
    qCInfo(lcServer) << "ServerService: session authenticated:" << sessionId;

    if (m_sessions.size() == 1 && m_capturePipeline) {
        QMetaObject::invokeMethod(m_capturePipeline, "startCapture", Qt::QueuedConnection);
    }

    emit clientAuthenticated(sessionId);
}

void ServerService::onSessionDisconnected(const QString &sessionId)
{
    qCInfo(lcServer) << "ServerService: session disconnected:" << sessionId;

    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i]->sessionId() == sessionId) {
            auto *session = m_sessions[i];
            if (m_capturePipeline) {
                QMetaObject::invokeMethod(m_capturePipeline, "unsubscribe",
                                          Qt::QueuedConnection,
                                          Q_ARG(ServerSession *, session));
            }
            m_sessions.removeAt(i);
            break;
        }
    }

    if (m_sessions.isEmpty() && m_capturePipeline) {
        QMetaObject::invokeMethod(m_capturePipeline, "stopCapture", Qt::QueuedConnection);
    }

    emit clientDisconnected(sessionId);
}
