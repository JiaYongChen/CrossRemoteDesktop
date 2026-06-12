#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include "../network/ConnectionManager.h"

class QWidget;
class ClientRemoteWindow;
class SessionManager;

/// 连接生命周期管理 — 从 ClientRemoteWindow 分离出的连接状态+标题+断连对话框
///
/// 管理连接状态机 → 窗口标题同步 → 断连对话框展示全流程。
class ConnectionLifecycle : public QObject {
    Q_OBJECT
public:
    explicit ConnectionLifecycle(QObject* parent = nullptr);

    /// 绑定目标窗口和会话管理器
    void manage(ClientRemoteWindow* window, SessionManager* sm);

    /// 设置连接状态（触发标题更新 + 断连处理）
    void setConnectionState(ConnectionManager::ConnectionState state);

    /// 查询当前状态
    ConnectionManager::ConnectionState connectionState() const;

    /// 设置主机名（用于标题显示）
    void setHostName(const QString& name);
    QString hostName() const;

private:
    void updateWindowTitle();
    void showDisconnectionDialog();

    ClientRemoteWindow* m_window = nullptr;
    SessionManager* m_sessionManager = nullptr;
    ConnectionManager::ConnectionState m_connectionState = ConnectionManager::Disconnected;
    QString m_hostName;
};
