// src/server/service/ServerService.h
#pragma once

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QObject>

#include "common/error/RdError.h"
#include "common/network/Protocol.h"

class CapturePipeline;
class ClipboardManager;
class FileTransferManager;
class IMessageCodec;
class QueueManager;
class ServerSession;
class SettingsManager;
class TcpListener;
class ThreadManager;
struct ClipboardMessage;

/**
 * @brief 服务端编排门面 — 管理 TcpListener + CapturePipeline + 会话生命周期
 *
 * 从 MainWindow 拆出，使服务端启动/停止/会话管理可脱离 UI 独立运作和测试。
 */
class ServerService : public QObject {
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Starting,
        Listening,
        Stopping
    };
    Q_ENUM(State)

    explicit ServerService(ThreadManager *threadManager,
                           QueueManager *queueManager,
                           SettingsManager *settingsManager,
                           QObject *parent = nullptr);
    ~ServerService() override;

    bool start(quint16 port);
    void stop();
    bool isRunning() const;
    quint16 port() const;

signals:
    void clientConnected(const QString &sessionId);
    void clientDisconnected(const QString &sessionId);
    void clientAuthenticated(const QString &sessionId);
    void errorOccurred(const RdError &error);

private slots:
    void onTcpListenerListening(quint16 port);
    void onTcpListenerStopped();
    void onTcpListenerError(const RdError &error);
    void onNewConnection(qintptr socketDescriptor);
    void onSessionAuthenticated(const QString &sessionId);
    void onSessionDisconnected(const QString &sessionId);

private:
    void startCapturePipeline();
    void stopCapturePipeline();
    void cleanupSessions();

    ThreadManager *m_threadManager;
    QueueManager *m_queueManager;
    SettingsManager *m_settingsManager;  ///< 配置管理器（start() 时重新读取凭据）

    TcpListener *m_tcpListener = nullptr;
    CapturePipeline *m_capturePipeline = nullptr;
    QList<ServerSession *> m_sessions;
    ClipboardManager *m_clipboardManager = nullptr;  ///< 服务端剪贴板（主线程）
    FileTransferManager *m_fileTransferManager = nullptr;  ///< 文件传输管理器（主线程）

    void broadcastClipboardToAllSessions(const ClipboardMessage& message);
    void onSessionClipboardData(const ClipboardMessage& message);
    void onSessionFileList(const ClipboardFileList& files, const QString& sessionId);
    void onFileContentRequest(quint32 fileIndex, const QString& sessionId);
    void onFileTransferInit(quint32 fileIndex, const QString& sessionId);
    void onFileChunkAck(quint32 fileIndex, quint32 ackSeq, const QString& sessionId);
    void onFileTransferCancelled(quint32 fileIndex, const QString& sessionId);
    void sendFileMessageToSession(const QString& sessionId, MessageType type, const IMessageCodec& message);

    /// 文件索引 → 请求会话 ID（服务端响应文件数据请求时回发定位用）
    QHash<quint32, QString> m_fileRequestSessions;

    State m_state = State::Stopped;
    quint16 m_port = 0;

    QString m_serverUsername;  ///< 服务端认证用户名（start() 时从 SettingsManager 刷新）
    QString m_serverPassword;  ///< 服务端认证密码（start() 时从 SettingsManager 刷新）
};
