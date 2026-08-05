#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include "common/error/RdError.h"

class QDialog;

/// 连接生命周期管理 — 从 ClientRemoteWindow 分离出的连接状态+标题+断连对话框
///
/// 纯传输层：不持有 ConnectionManager 状态，直接响应其语义化事件信号；
/// 内部维护私有 DisplayState 仅用于窗口标题与终端处理（断连对话框/静默关闭）。
class ConnectionLifecycle : public QObject {
    Q_OBJECT
public:
    explicit ConnectionLifecycle(QObject* parent = nullptr);

    /// 绑定目标窗口
    void manage(QWidget* window);

    // ── 事件槽（由 ConnectionManager 语义化事件信号直接驱动）──
    void onConnecting();
    void onConnected();
    void onDisconnected();
    void onReconnecting();
    void onErrorOccurred(const RdError& error);

    /// 认证重试挂起标记：凭据/版本对话框展示期间抑制终端处理（关窗/断连框）。
    /// 由 RemoteDesktopSession 在认证失败弹框前设置，对话框关闭后复位
    void setAuthRetryPending(bool pending);

    /// 设置主机名（用于标题显示）
    void setHostName(const QString& name);

    /// 设置仅查看模式（影响窗口标题后缀）
    void setViewOnly(bool viewOnly) { m_viewOnly = viewOnly; updateWindowTitle(); }

    /// 弹出服务端身份变更警告（TOFU Changed）；用户选择后经 trustDecision 回传
    void showTrustWarning(const QString& endpoint, const QString& oldFingerprint,
                          const QString& newFingerprint);

signals:
    /// 用户对信任警告的决策：accept=true 信任新证书并继续，false 取消。
    /// 决策上下文（endpoint/最新指纹）由 ConnectionManager 持有，此处仅回传接受与否
    void trustDecision(bool accept);

private:
    /// 传输层展示状态（仅驱动窗口标题与终端处理，非协议状态机）
    enum class DisplayState { Disconnected, Connecting, Connected, Reconnecting, Error };

    void setDisplayState(DisplayState state);
    void updateWindowTitle();
    void showDisconnectionDialog();

    /// 收起信任对话框（状态离开挂起态/对话框替换时）：先断决策回环再销毁
    void dismissTrustDialog();

    QWidget* m_window = nullptr;
    DisplayState m_displayState = DisplayState::Disconnected;
    QString m_hostName;
    bool m_viewOnly = false;

    /// 是否曾建立会话（进入 Connected 后置位）——用于区分"真实会话丢失"和"初始连接失败"
    bool m_wasAuthenticated = false;

    /// 认证重试挂起——凭据/版本对话框展示期间抑制终端处理（关窗/断连框）
    bool m_authRetryPending = false;

    /// 弹窗重入守卫——QMessageBox::exec() 运行嵌套事件循环，
    /// 期间可能有新的状态变更触发第二次弹窗调度
    bool m_dialogShowing = false;

    QDialog* m_trustDialog = nullptr;                 ///< 非模态信任警告对话框（parent 为窗口，随窗口销毁）
};
