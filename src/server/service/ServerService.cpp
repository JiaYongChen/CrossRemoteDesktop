// src/server/service/ServerService.cpp
#include "server/service/ServerService.h"

#include <QtCore/QTimer>

#include "common/clipboard/ClipboardManager.h"
#include "common/config/SettingsManager.h"
#include "common/crypto/PasswordCrypto.h"
#include "common/error/ErrorCode.h"
#include "common/error/RdError.h"
#include "common/logging/LoggingCategories.h"
#include "common/network/Protocol.h"
#include "common/threading/ThreadManager.h"
#include "server/capture/CapturePipeline.h"
#include "server/dataflow/QueueManager.h"
#include "server/listener/TcpListener.h"
#include "server/session/ServerSession.h"

ServerService::ServerService(ThreadManager *threadManager,
                             QueueManager *queueManager,
                             SettingsManager *settingsManager,
                             QObject *parent)
    : QObject(parent)
    , m_threadManager(threadManager)
    , m_queueManager(queueManager)
{
    // 读取服务端认证配置
    m_serverUsername = settingsManager->getString("Server/username");
    QString encryptedPwd = settingsManager->getString("Server/password");
    if (!encryptedPwd.isEmpty() && !m_serverUsername.isEmpty()) {
        m_serverPassword = PasswordCrypto::decrypt(m_serverUsername, encryptedPwd);
        qCInfo(lcServer) << "ServerService: 密码认证已启用，用户名:" << m_serverUsername;
    } else {
        qCInfo(lcServer) << "ServerService: 无密码认证模式";
    }

    m_clipboardManager = new ClipboardManager(this);
    m_clipboardManager->setEnabled(true);

    // 服务端本地文本复制 → 广播到所有客户端
    connect(m_clipboardManager, &ClipboardManager::clipboardTextChanged,
            this, [this](const QString& text) {
                ClipboardMessage msg(text);
                broadcastClipboardToAllSessions(msg);
            });

    // 服务端本地图片复制 → 广播到所有客户端
    connect(m_clipboardManager, &ClipboardManager::clipboardImageChanged,
            this, [this](const QByteArray& imageData, quint32 width, quint32 height) {
                ClipboardMessage msg(imageData, width, height);
                broadcastClipboardToAllSessions(msg);
            });
}

ServerService::~ServerService()
{
    if (m_clipboardManager) {
        m_clipboardManager->setEnabled(false);
    }
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
                                  Q_ARG(quint16, m_port),
                                  Q_ARG(QString, m_serverPassword));
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
                                                    m_threadManager,
                                                    m_serverUsername,
                                                    m_serverPassword);
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
    connect(sessionPtr, &ServerSession::clipboardDataReceived,
            this, &ServerService::onSessionClipboardData);

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

void ServerService::onSessionClipboardData(const ClipboardMessage& message) {
    // 1. 写入服务端系统剪贴板（含去重标记防止回环）
    if (message.isText()) {
        m_clipboardManager->applyRemoteText(message.text());
    } else if (message.isImage()) {
        m_clipboardManager->applyRemoteImage(message.imageData());
    }

    // 2. 广播给所有已认证会话（发送者客户端因 m_lastText 匹配而静默跳过）
    broadcastClipboardToAllSessions(message);
}

void ServerService::broadcastClipboardToAllSessions(const ClipboardMessage& message) {
    // 预编码消息包（一次性，所有 session 复用相同字节）
    QByteArray encoded = Protocol::createMessage(MessageType::CLIPBOARD_DATA, message);
    if (encoded.isEmpty()) return;

    for (auto* session : m_sessions) {
        if (session->isAuthenticated()) {
            QMetaObject::invokeMethod(session,
                                      &ServerSession::sendClipboardData,
                                      Qt::QueuedConnection,
                                      encoded);
        }
    }
}
