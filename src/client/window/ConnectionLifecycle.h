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

    /// 设置关联的 ConnectionManager 指针（用于重试认证）
    void setConnectionManager(ConnectionManager* mgr);

    /// 缓存认证错误消息（由连接层错误信号触发）
    void setAuthErrorMessage(const QString& msg);

    /// 缓存用户名（用于凭据重输对话框预填）
    void setCachedUsername(const QString& name);

signals:
    /// 用户请求重试认证（携带新凭据），由顶层组件（ClientRemoteWindow/MainWindow）连接处理
    void retryAuthRequested(const QString& username, const QString& password);

private:
    void updateWindowTitle();
    void showDisconnectionDialog();

    /// @param passwordOnly true=仅重输密码（用户名正确），false=重输用户名+密码
    void showCredentialDialog(const QString& errorMessage, bool passwordOnly);

    QWidget* m_window = nullptr;
    ConnectionManager::ConnectionState m_connectionState = ConnectionManager::Disconnected;
    QString m_hostName;
    bool m_viewOnly = false;

    /// 是否曾完成认证——用于区分"真实会话丢失"和"初始连接失败"
    bool m_wasAuthenticated = false;

    /// 弹窗重入守卫——QMessageBox::exec() 运行嵌套事件循环，
    /// 期间可能有新的状态变更触发第二次弹窗调度
    bool m_dialogShowing = false;

    ConnectionManager* m_connectionManager = nullptr;
    QString m_authErrorMessage;           ///< 缓存认证错误消息
    QString m_cachedUsername;             ///< 预填用户名
    QString m_pendingAuthError;           ///< 缓存认证错误消息（供异步弹窗使用）
    bool m_authRetryPending = false;      ///< 是否有待处理的重试
};
