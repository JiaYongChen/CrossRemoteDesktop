#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include "error/RdError.h"

/**
 * @brief 建连参数 — 从 ConnectionDialog 提取的完整连接配置
 *
 * 纯值类型，不含 QObject 继承。由 MainWindow 从对话框提取、
 * 传递给 RemoteDesktopSession，在内部各 create*() 阶段分发到子组件。
 */
struct ConnectionParams {
    // ── 连接目标（必填）──
    QString host;
    int     port = 5921;

    // ── 连接信息标签页 ──
    QString hostname;       // 可选友好名称（用于窗口标题）
    QString username;       // 可选用户名
    QString password;       // 可选密码

    // ── 显示 & 功能标签页 ──
    bool    fullScreen     = false;
    int     windowWidth    = 1024;
    int     windowHeight   = 768;
    bool    viewOnly       = false;
    bool    shareClipboard = true;
    bool    showCursor     = true;

    // ── 网络标签页 ──
    int     connectionTimeout  = 30000;   // 毫秒
    bool    autoReconnect      = false;
    int     reconnectInterval  = 5000;    // 毫秒
};

class ConnectionManager;
class ProtocolSession;
class DecodePipeline;
class ClientRemoteWindow;

/**
 * @brief 远程桌面会话 — 创建并组装一个连接的全部组件，管理完整生命周期
 *
 * 归属 Main 线程。所有网络对象（TcpClient/ConnectionManager/ProtocolSession）
 * 和管线对象（DecodePipeline）均在 Main 线程创建和运行。
 * QSslSocket 异步 I/O 不阻塞 GUI；仅解码在独立 DecodeThread 进行。
 * close() 逆序清理。
 */
class RemoteDesktopSession : public QObject {
    Q_OBJECT
public:
    explicit RemoteDesktopSession(const ConnectionParams& params,
                                  const QString& connectionId,
                                  QObject* parent = nullptr);
    ~RemoteDesktopSession() override;

    /// 启动连接：直接调用 connectToHost（同线程，无需 invokeMethod）
    void start();

    /// 优雅关闭（幂等）：停止管线 → 断开连接 → 关闭窗口
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
    ConnectionParams m_params;
    bool    m_closing = false;

    // ── 网络层 ──
    ConnectionManager* m_connectionManager = nullptr;  // 内部自建 TcpClient
    ProtocolSession*   m_protocolSession = nullptr;

    // ── 管线层 ──
    DecodePipeline* m_decodePipeline = nullptr;

    // ── UI 层 ──
    ClientRemoteWindow* m_window = nullptr;
};
