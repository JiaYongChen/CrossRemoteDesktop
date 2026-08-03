#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include "client/network/ConnectionManager.h"
#include "common/error/ErrorCode.h"

class QDialog;

/// 连接生命周期管理 — 从 ClientRemoteWindow 分离出的连接状态+标题+断连对话框
///
/// 管理连接状态机 → 窗口标题同步 → 断连对话框/凭据重输对话框展示全流程。
class ConnectionLifecycle : public QObject {
    Q_OBJECT
public:
    explicit ConnectionLifecycle(QObject* parent = nullptr);

    /// 绑定目标窗口
    void manage(QWidget* window);

    /// 设置连接状态（触发标题更新 + 断连/凭据重输处理）
    void setConnectionState(ConnectionManager::ConnectionState state);

    /// 设置主机名（用于标题显示）
    void setHostName(const QString& name);

    /// 设置仅查看模式（影响窗口标题后缀）
    void setViewOnly(bool viewOnly) { m_viewOnly = viewOnly; updateWindowTitle(); }

    /// 缓存认证错误码（用于判定可重试/密码模式选择，由 errorOccurred 信号触发）
    void setAuthErrorCode(ErrorCode code);

    /// 缓存认证错误消息（用于对话框展示，由 errorOccurred 信号触发）
    void setAuthErrorMessage(const QString& msg);

    /// 缓存用户名（用于凭据重输对话框预填）
    void setCachedUsername(const QString& name);

    /// 弹出服务端身份变更警告（TOFU Changed）；用户选择后经 trustDecision 回传
    void showTrustWarning(const QString& endpoint, const QString& oldFingerprint,
                          const QString& newFingerprint);

signals:
    /// 用户请求重试认证（携带新凭据），由顶层组件（ClientRemoteWindow/MainWindow）连接处理
    void retryAuthRequested(const QString& username, const QString& password);

    /// 用户对信任警告的决策：accept=true 信任新证书并继续，false 取消。
    /// 决策上下文（endpoint/最新指纹）由 ConnectionManager 持有，此处仅回传接受与否
    void trustDecision(bool accept);

private:
    void updateWindowTitle();
    void showDisconnectionDialog();

    /// 收起信任对话框（状态离开挂起态/对话框替换时）：先断决策回环再销毁
    void dismissTrustDialog();

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

    ErrorCode m_authErrorCode = ErrorCode::Unknown;  ///< 认证错误类型（结构化解耦，不依赖字符串匹配）
    QString m_authErrorMessage;                       ///< 缓存认证错误消息（用于对话框展示）
    QString m_cachedUsername;                         ///< 预填用户名
    bool m_authRetryPending = false;                  ///< 是否有待处理的重试（终态守卫 + 重入守卫）

    QDialog* m_trustDialog = nullptr;                 ///< 非模态信任警告对话框（parent 为窗口，随窗口销毁）
};
