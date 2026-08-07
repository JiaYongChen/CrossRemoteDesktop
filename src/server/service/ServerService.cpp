// src/server/service/ServerService.cpp
#include "server/service/ServerService.h"

#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>

#include "common/clipboard/ClipboardManager.h"
#include "common/config/SettingsManager.h"
#include "common/crypto/PasswordCrypto.h"
#include "common/error/ErrorCode.h"
#include "common/error/RdError.h"
#include "common/logging/LoggingCategories.h"
#include "common/network/Protocol.h"
#include "common/threading/ThreadManager.h"
#include "core/transfer/FileTransferManager.h"
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
    , m_settingsManager(settingsManager)
{
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

    // 文件传输管理器（接收目录默认下载目录，可在 SettingsManager 配置覆盖）
    const QString downloadDir = m_settingsManager
        ? m_settingsManager->getString(QStringLiteral("fileTransfer.downloadDir"),
              QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
        : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    m_fileTransferManager = new FileTransferManager(downloadDir, this);

    // 超时检测定时器：每秒检查一次，自动取消挂死传输
    auto* timeoutTimer = new QTimer(this);
    connect(timeoutTimer, &QTimer::timeout, this, [this]() {
        m_fileTransferManager->checkTimeouts();
    });
    timeoutTimer->start(2000);  // 2s 间隔（>= 超时阈值的粒度够用）

    // 服务端本地文件复制 → 广播 FILE_LIST 到所有客户端
    connect(m_clipboardManager, &ClipboardManager::clipboardFilesChanged,
            this, [this](const ClipboardFileList& files) {
                ClipboardMessage msg(files);
                broadcastClipboardToAllSessions(msg);
            });

    // 文件数据回发：lastChunk=true 走小文件单块通道，lastChunk=false 走大文件分块通道
    connect(m_fileTransferManager, &FileTransferManager::fileChunkReady,
            this, [this](int fileIndex, const QByteArray& chunk, quint32 seq, bool lastChunk) {
                const quint32 index = static_cast<quint32>(fileIndex);
                if (lastChunk) {
                    ClipboardFileChunk msg;
                    msg.fileIndex = index;
                    msg.flags = 0x01;  // lastChunk
                    msg.data = chunk;
                    sendFileMessageToSession(m_fileRequestSessions.value(index),
                                             MessageType::CLIPBOARD_FILE_CHUNK, msg);
                } else {
                    FileTransferChunk msg;
                    msg.fileIndex = index;
                    msg.seq = seq;
                    msg.data = chunk;
                    sendFileMessageToSession(m_fileRequestSessions.value(index),
                                             MessageType::FILE_TRANSFER_CHUNK, msg);
                }
            });
    // 粘贴方接收大文件块 → 回发 ACK 推进复制方滑动窗口
    connect(m_fileTransferManager, &FileTransferManager::fileChunkAckNeeded,
            this, [this](int fileIndex, quint32 seq) {
                FileTransferAck ack;
                ack.fileIndex = static_cast<quint32>(fileIndex);
                ack.ackSeq = seq;
                sendFileMessageToSession(m_fileRequestSessions.value(ack.fileIndex),
                                         MessageType::FILE_TRANSFER_ACK, ack);
            });

    // 传输结束（完成/错误）清理会话映射 + 中继
    connect(m_fileTransferManager, &FileTransferManager::transferComplete,
            this, [this](int fileIndex, const QString& savedPath) {
                m_fileRequestSessions.remove(fileIndex);
                m_fileRelays.remove(static_cast<quint32>(fileIndex));
                qCInfo(lcServer) << "文件传输完成:" << fileIndex << savedPath;
            });
    connect(m_fileTransferManager, &FileTransferManager::transferError,
            this, [this](int fileIndex, const QString& errorMessage) {
                // 回发 CANCEL 通知请求方（避免上下文悬挂）
                const QString reqSession = m_fileRequestSessions.value(fileIndex);
                if (!reqSession.isEmpty()) {
                    FileTransferCancel cancel;
                    cancel.fileIndex = static_cast<quint32>(fileIndex);
                    sendFileMessageToSession(reqSession, MessageType::FILE_TRANSFER_CANCEL, cancel);
                }
                m_fileRequestSessions.remove(fileIndex);
                m_fileRelays.remove(static_cast<quint32>(fileIndex));
                qCWarning(lcServer) << "文件传输错误:" << fileIndex << errorMessage;
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

    // 每次启动时重新读取认证配置（支持 stop/start 后凭据变更生效）
    m_serverUsername = m_settingsManager->getString("Server/username").trimmed();
    QString encryptedPwd = m_settingsManager->getString("Server/password");
    m_serverPassword.clear();
    if (!encryptedPwd.isEmpty() && !m_serverUsername.isEmpty()) {
        m_serverPassword = PasswordCrypto::decrypt(m_serverUsername, encryptedPwd);
        if (m_serverPassword.isEmpty()) {
            qCWarning(lcServer) << "ServerService: 密码解密失败——认证已禁用！"
                                << "加密数据可能已损坏，请重新设置密码";
        } else {
            qCInfo(lcServer) << "ServerService: 密码认证已启用，用户名:" << m_serverUsername;
        }
    } else {
        qCInfo(lcServer) << "ServerService: 无密码认证模式";
    }

    // 1. 创建 TcpListener
    if (!m_threadManager->hasThread("TcpListener")) {
        auto listener = std::make_unique<TcpListener>(nullptr, m_settingsManager);
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
                                  Q_ARG(quint16, m_port));
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

    if (m_fileTransferManager) {
        m_fileTransferManager->cancelAllTransfers();
    }

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

    // 每次新连接时实时读取凭据，使 SettingsDialog 即时变更立即生效
    QString username = m_settingsManager->getString("Server/username").trimmed();
    QString password;
    QString encryptedPwd = m_settingsManager->getString("Server/password");
    if (!encryptedPwd.isEmpty() && !username.isEmpty()) {
        password = PasswordCrypto::decrypt(username, encryptedPwd);
    }

    auto session = std::make_unique<ServerSession>(socketDescriptor, cert, key,
                                                    m_threadManager,
                                                    username, password);
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

    // ── 文件传输信号接线 ──
    connect(sessionPtr, &ServerSession::clipboardFileListReceived,
            this, [this](const ClipboardFileList& files, const QString& sessionId) {
                onSessionFileList(files, sessionId);
            });
    connect(sessionPtr, &ServerSession::fileContentRequestReceived,
            this, &ServerService::onFileContentRequest);
    connect(sessionPtr, &ServerSession::fileTransferInitReceived,
            this, &ServerService::onFileTransferInit);
    connect(sessionPtr, &ServerSession::clipboardFileChunkReceived,
            this, [this](quint32 fileIndex, const QByteArray& data, quint8 flags, const QString& sessionId) {
                // 中继转发：源会话回发的数据块 → 目标会话
                auto relayIt = m_fileRelays.find(fileIndex);
                if (relayIt != m_fileRelays.end() && relayIt->sourceSession == sessionId) {
                    ClipboardFileChunk chunk;
                    chunk.fileIndex = fileIndex;
                    chunk.flags = flags;
                    chunk.data = data;
                    sendFileMessageToSession(relayIt->destSession, MessageType::CLIPBOARD_FILE_CHUNK, chunk);
                    return;
                }
                m_fileTransferManager->handleIncomingChunk(fileIndex, data, (flags & 0x01) != 0);
            });
    connect(sessionPtr, &ServerSession::fileTransferChunkReceived,
            this, [this](quint32 fileIndex, quint32 seq, const QByteArray& data, const QString& sessionId) {
                // 中继转发
                auto relayIt = m_fileRelays.find(fileIndex);
                if (relayIt != m_fileRelays.end() && relayIt->sourceSession == sessionId) {
                    FileTransferChunk chunk;
                    chunk.fileIndex = fileIndex;
                    chunk.seq = seq;
                    chunk.data = data;
                    sendFileMessageToSession(relayIt->destSession, MessageType::FILE_TRANSFER_CHUNK, chunk);
                    return;
                }
                m_fileRequestSessions.insert(fileIndex, sessionId);
                m_fileTransferManager->handleIncomingChunk(fileIndex, data, false, seq);
            });
    connect(sessionPtr, &ServerSession::fileChunkAckReceived,
            this, [this](quint32 fileIndex, quint32 ackSeq, const QString& sessionId) {
                // 中继转发：目标会话的 ACK → 源会话
                auto relayIt = m_fileRelays.find(fileIndex);
                if (relayIt != m_fileRelays.end() && relayIt->destSession == sessionId) {
                    FileTransferAck ack;
                    ack.fileIndex = fileIndex;
                    ack.ackSeq = ackSeq;
                    sendFileMessageToSession(relayIt->sourceSession, MessageType::FILE_TRANSFER_ACK, ack);
                    return;
                }
                onFileChunkAck(fileIndex, ackSeq, sessionId);
            });
    connect(sessionPtr, &ServerSession::fileTransferCancelled,
            this, [this](quint32 fileIndex, const QString& sessionId) {
                // 中继转发 + 清理（仅中继参与者可取消）
                auto relayIt = m_fileRelays.find(fileIndex);
                if (relayIt != m_fileRelays.end()
                    && (relayIt->sourceSession == sessionId
                        || relayIt->destSession == sessionId)) {
                    const QString other = (relayIt->sourceSession == sessionId)
                        ? relayIt->destSession : relayIt->sourceSession;
                    FileTransferCancel cancel;
                    cancel.fileIndex = fileIndex;
                    sendFileMessageToSession(other, MessageType::FILE_TRANSFER_CANCEL, cancel);
                    m_fileRelays.erase(relayIt);
                    return;
                }
                onFileTransferCancelled(fileIndex, sessionId);
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

    // 无条件触发捕获启动：startCapture() 内部已有 m_captureActive 幂等守卫，
    // 多会话/残留会话场景下安全。原先的 m_sessions.size()==1 判断在会话清理
    // 异常（僵尸会话残留）时会恒不为 1，导致认证成功后捕获永不启动（黑屏）。
    if (m_capturePipeline) {
        QMetaObject::invokeMethod(m_capturePipeline, "startCapture", Qt::QueuedConnection);
    }

    emit clientAuthenticated(sessionId);
}

void ServerService::onSessionDisconnected(const QString &sessionId)
{
    qCInfo(lcServer) << "ServerService: session disconnected:" << sessionId;

    // 清理该会话关联的文件列表 + 文件传输映射
    m_sessionFileLists.remove(sessionId);

    for (auto it = m_fileRequestSessions.begin(); it != m_fileRequestSessions.end();) {
        if (it.value() == sessionId) {
            it = m_fileRequestSessions.erase(it);
        } else {
            ++it;
        }
    }

    // 清理该会话参与的所有转发中继（发送 CANCEL 给对方）
    for (auto it = m_fileRelays.begin(); it != m_fileRelays.end(); ) {
        if (it.value().sourceSession == sessionId || it.value().destSession == sessionId) {
            FileTransferCancel cancel;
            cancel.fileIndex = it.key();
            const QString other = (it.value().sourceSession == sessionId)
                ? it.value().destSession : it.value().sourceSession;
            sendFileMessageToSession(other, MessageType::FILE_TRANSFER_CANCEL, cancel);
            it = m_fileRelays.erase(it);
        } else {
            ++it;
        }
    }

    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i]->sessionId() == sessionId) {
            auto *session = m_sessions[i];
            if (m_capturePipeline) {
                QMetaObject::invokeMethod(m_capturePipeline, "unsubscribe",
                                          Qt::QueuedConnection,
                                          Q_ARG(ServerSession *, session));
            }

            // 销毁会话线程——shutdown() 已在本线程完成子线程清理，此处回收 ServerSession
            // 线程自身。否则线程泄漏累积，且 socket 描述符被 OS 复用后新连接会因线程名
            // "ServerSession_<描述符>" 撞名导致 createThread 失败、连接无法建立。
            const QString threadName = QString("ServerSession_%1").arg(session->socketDescriptor());
            if (m_threadManager && m_threadManager->hasThread(threadName)) {
                (void)m_threadManager->stopThread(threadName, true);
                (void)m_threadManager->destroyThread(threadName);
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
    } else if (message.isFileList()) {
        // FILE_LIST 由 onSessionFileList 走独立路径（携带 sessionId），此处不应到达
        qCDebug(lcServer) << "onSessionClipboardData - 收到 FILE_LIST（应走 clipboardFileListReceived）";
        onSessionFileList(message.fileList(), QString());
    }

    // 2. 广播给所有已认证会话（发送者客户端因 m_lastText 匹配而静默跳过）
    broadcastClipboardToAllSessions(message);
}

void ServerService::onSessionFileList(const ClipboardFileList& files, const QString& sessionId) {
    // 存储该会话的 FILE_LIST（跨客户端请求路由时定位源会话）
    m_sessionFileLists.insert(sessionId, files);

    // 广播远端文件列表（排除发送者）
    ClipboardMessage msg(files);
    const QByteArray encoded = Protocol::createMessage(MessageType::CLIPBOARD_DATA, msg);
    if (encoded.isEmpty()) return;

    for (auto* session : m_sessions) {
        if (session->isAuthenticated() && session->sessionId() != sessionId) {
            QMetaObject::invokeMethod(session, &ServerSession::sendClipboardData,
                                      Qt::QueuedConnection, encoded);
        }
    }
}

void ServerService::onFileContentRequest(quint32 fileIndex, const QString& sessionId) {
    if (!prepareFileSend(fileIndex, sessionId, false)) {
        // 请求被拒：回发 CANCEL 通知客户端（避免上下文悬挂）
        FileTransferCancel cancel;
        cancel.fileIndex = fileIndex;
        sendFileMessageToSession(sessionId, MessageType::FILE_TRANSFER_CANCEL, cancel);
    }
}

void ServerService::onFileTransferInit(quint32 fileIndex, const QString& sessionId) {
    if (!prepareFileSend(fileIndex, sessionId, true)) {
        FileTransferCancel cancel;
        cancel.fileIndex = fileIndex;
        sendFileMessageToSession(sessionId, MessageType::FILE_TRANSFER_CANCEL, cancel);
    }
}

bool ServerService::prepareFileSend(quint32 fileIndex, const QString& sessionId, bool requireLarge) {
    // 1) 优先尝试服务端本地剪贴板
    const ClipboardFileList localList = m_clipboardManager->lastFileList();
    if (fileIndex < static_cast<quint32>(localList.files.size())) {
        const quint64 fileSize = localList.files.at(fileIndex).fileSize;
        const bool isLarge = fileSize > FileTransferManager::kSmallFileThreshold;
        if (isLarge != requireLarge) {
            qCWarning(lcServer) << (isLarge ? "大文件应走 FILE_TRANSFER_INIT"
                                            : "小文件应走 CLIPBOARD_FILE_REQUEST")
                                << "，忽略请求:" << fileIndex;
            return false;
        }
        const QString sourcePath = m_clipboardManager->lastFilePath(fileIndex);
        if (!sourcePath.isEmpty()) {
            if (m_fileRequestSessions.contains(fileIndex)) {
                qCWarning(lcServer) << "该文件索引已有传输，拒绝重复请求:" << fileIndex;
                return false;
            }
            m_fileRequestSessions.insert(fileIndex, sessionId);
            m_fileTransferManager->handleFileRequest(fileIndex, localList, sourcePath);
            return true;
        }
    }

    // 2) 服务端本地无匹配 → 跨客户端转发：查找持有该文件索引的会话
    for (auto it = m_sessionFileLists.begin(); it != m_sessionFileLists.end(); ++it) {
        const QString& srcSession = it.key();
        const ClipboardFileList& srcList = it.value();
        if (srcSession == sessionId || fileIndex >= static_cast<quint32>(srcList.files.size()))
            continue;

        const quint64 fileSize = srcList.files.at(fileIndex).fileSize;
        if ((fileSize > FileTransferManager::kSmallFileThreshold) != requireLarge)
            continue;

        if (m_fileRelays.contains(fileIndex)) {
            qCWarning(lcServer) << "该文件索引已有中继传输，拒绝:" << fileIndex;
            return false;
        }

        // 建立转发中继：源会话持有文件 → 转发请求给源
        m_fileRelays.insert(fileIndex, {srcSession, sessionId});

        if (requireLarge) {
            FileTransferInit init;
            init.fileIndex = fileIndex;
            sendFileMessageToSession(srcSession, MessageType::FILE_TRANSFER_INIT, init);
        } else {
            ClipboardFileRequest req;
            req.fileIndex = fileIndex;
            sendFileMessageToSession(srcSession, MessageType::CLIPBOARD_FILE_REQUEST, req);
        }

        qCInfo(lcServer) << "跨客户端文件转发:"
                         << "src=" << srcSession << "dst=" << sessionId
                         << "file=" << srcList.files.at(fileIndex).fileName;
        return true;
    }

    qCWarning(lcServer) << "无法定位文件归属:" << fileIndex;
    return false;
}

void ServerService::onFileChunkAck(quint32 fileIndex, quint32 ackSeq, const QString& sessionId) {
    if (m_fileRequestSessions.value(fileIndex) != sessionId) {
        qCWarning(lcServer) << "拒绝非归属会话的 ACK:" << sessionId << "fileIndex:" << fileIndex;
        return;
    }
    m_fileTransferManager->handleAck(fileIndex, ackSeq);
}

void ServerService::onFileTransferCancelled(quint32 fileIndex, const QString& sessionId) {
    if (m_fileRequestSessions.value(fileIndex) != sessionId) {
        qCWarning(lcServer) << "拒绝非归属会话的取消:" << sessionId << "fileIndex:" << fileIndex;
        return;
    }
    m_fileRequestSessions.remove(fileIndex);
    m_fileTransferManager->cancelTransfer(fileIndex);
    qCInfo(lcServer) << "文件传输已取消: fileIndex=" << fileIndex;
}

void ServerService::sendFileMessageToSession(const QString& sessionId, MessageType type, const IMessageCodec& message) {
    if (sessionId.isEmpty()) {
        qCWarning(lcServer) << "文件消息回发失败：无目标会话";
        return;
    }

    ServerSession* target = nullptr;
    for (auto* session : m_sessions) {
        if (session->sessionId() == sessionId) {
            target = session;
            break;
        }
    }
    if (!target || !target->isAuthenticated()) {
        qCWarning(lcServer) << "文件消息回发失败：会话不存在或未认证";
        return;
    }

    const QByteArray encoded = Protocol::createMessage(type, message);
    if (encoded.isEmpty()) return;

    // lambda 上下文绑定 target：会话销毁时待处理调用自动取消，避免悬垂指针
    QMetaObject::invokeMethod(target,
                              [target, type, encoded]() {
                                  target->sendEncodedFileMessage(type, encoded);
                              },
                              Qt::QueuedConnection);
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
