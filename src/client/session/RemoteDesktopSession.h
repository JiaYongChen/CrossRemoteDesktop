#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

#include "common/data/ConnectionParams.h"
#include "common/error/RdError.h"

class ClientRemoteWindow;
class ConnectionManager;
class DecodePipeline;
class FileTransferManager;
class ProtocolSession;
class SettingsManager;

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
                                  SettingsManager& settings,
                                  QObject* parent = nullptr);
    ~RemoteDesktopSession() override;

    /// 启动连接：直接调用 connectToHost（同线程，无需 invokeMethod）
    void start();

    /// 优雅关闭（幂等）：停止管线 → 断开连接 → 关闭窗口
    void close();

signals:
    /// 连接完全关闭（RemoteDesktopSession 可安全销毁）
    void finished(const QString& connectionId);

    /// 连接错误
    void errorOccurred(const RdError& error);

    /// 认证成功——携带实际通过验证的凭据（重试后可能与初始入参不同），供上层回写连接历史
    void authenticated(const QString& connectionId, const QString& username,
                       const QString& password);

private:
    void createNetworkComponents();
    void createDecodePipeline();
    void createWindow();
    void wireSignals();

    /// 凭据重输对话框（认证失败 INVALID_CREDENTIALS 时展示；重试直连重连）
    void showCredentialDialog(const QString& errorMessage);

    /// 版本不兼容对话框（握手版本闸门不通过时展示；确认后关闭窗口）
    void showVersionMismatchDialog(const QString& serverVer, const QString& localVer);

    QString m_connectionId;
    ConnectionParams m_params;
    SettingsManager* m_settings = nullptr;   ///< 持久化入口（注入 ConnectionManager 启用 TOFU）
    bool    m_closing = false;

    // ── 网络层 ──
    ConnectionManager* m_connectionManager = nullptr;  // 内部自建 TcpClient
    ProtocolSession*   m_protocolSession = nullptr;

    // ── 文件传输层 ──
    FileTransferManager* m_fileTransferManager = nullptr;  // 大文件分块收发 + ACK 流控

    // ── 管线层 ──
    DecodePipeline* m_decodePipeline = nullptr;

    // ── UI 层 ──
    ClientRemoteWindow* m_window = nullptr;
    bool m_authDialogPending = false;  ///< 重入守卫：防 200ms 窗口期重复弹凭据框
};
