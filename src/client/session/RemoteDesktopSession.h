#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include "error/RdError.h"

class QThread;
class TcpClient;
class ConnectionManager;
class ProtocolSession;
class DecodePipeline;
class ClientRemoteWindow;

/**
 * @brief 远程桌面会话 — 创建并组装一个连接的全部组件，管理完整生命周期
 *
 * 归属 Main 线程。构造时同步创建所有子组件并完成信号接线。
 * start() 启动 Network 线程并异步发起连接。
 * close() 逆序清理并自毁。
 */
class RemoteDesktopSession : public QObject {
    Q_OBJECT
public:
    explicit RemoteDesktopSession(const QString& host, int port,
                                  const QString& connectionId,
                                  QObject* parent = nullptr);
    ~RemoteDesktopSession() override;

    /// 启动连接：启动 Network 线程 → 异步调用 connectToHost
    void start();

    /// 优雅关闭（幂等）：停止管线 → 断开连接 → 停止线程 → 关闭窗口
    void close();

    QString connectionId() const { return m_connectionId; }

    // ── 组件访问（供 MainWindow 查询状态）──
    ProtocolSession*   protocolSession() const   { return m_protocolSession; }
    ConnectionManager* connectionManager() const  { return m_connectionManager; }
    ClientRemoteWindow* remoteWindow() const      { return m_window; }
    DecodePipeline*    decodePipeline() const     { return m_decodePipeline; }

signals:
    /// 连接完全关闭（RemoteDesktopSession 可安全销毁）
    void finished(const QString& connectionId);

    /// 连接错误
    void errorOccurred(const RdError& error);

private:
    void createNetworkComponents();
    void createDecodePipeline();
    void createWindow();
    void wireSignals();

    QString m_connectionId;
    QString m_host;
    int     m_port;
    bool    m_closing = false;

    // ── 网络层（Network 线程）──
    QThread*           m_networkThread = nullptr;
    ConnectionManager* m_connectionManager = nullptr;  // 内部自建 TcpClient
    ProtocolSession*   m_protocolSession = nullptr;

    // ── 管线层（Network 线程）──
    DecodePipeline* m_decodePipeline = nullptr;

    // ── UI 层（Main 线程）──
    ClientRemoteWindow* m_window = nullptr;
};
