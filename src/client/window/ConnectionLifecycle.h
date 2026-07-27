#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include "client/network/ConnectionManager.h"

/// 连接生命周期管理 — 从 ClientRemoteWindow 分离出的连接状态+标题+断连对话框
///
/// 管理连接状态机 → 窗口标题同步 → 断连对话框展示全流程。
class ConnectionLifecycle : public QObject {
    Q_OBJECT
public:
    explicit ConnectionLifecycle(QObject* parent = nullptr);

    /// 绑定目标窗口
    void manage(QWidget* window);

    /// 设置连接状态（触发标题更新 + 断连处理）
    void setConnectionState(ConnectionManager::ConnectionState state);

    /// 设置主机名（用于标题显示）
    void setHostName(const QString& name);

    /// 设置仅查看模式（影响窗口标题后缀）
    void setViewOnly(bool viewOnly) { m_viewOnly = viewOnly; updateWindowTitle(); }

private:
    void updateWindowTitle();
    void showDisconnectionDialog();

    QWidget* m_window = nullptr;
    ConnectionManager::ConnectionState m_connectionState = ConnectionManager::Disconnected;
    QString m_hostName;
    bool m_viewOnly = false;

    /// 是否曾完成认证——用于区分"真实会话丢失"和"初始连接失败"
    bool m_wasAuthenticated = false;

    /// 弹窗重入守卫——QMessageBox::exec() 运行嵌套事件循环，
    /// 期间可能有新的状态变更触发第二次弹窗调度
    bool m_dialogShowing = false;
};
