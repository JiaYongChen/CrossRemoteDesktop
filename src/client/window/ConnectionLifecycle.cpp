#include "ConnectionLifecycle.h"

#include <QtCore/QTimer>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include "common/logging/LoggingCategories.h"

namespace {
/// 将 hex 指纹格式化为 SSH 风格冒号分隔大写对（AB:CD:…），便于人工核对
QString formatFingerprint(const QString& hex) {
    const QString upper = hex.toUpper();
    QString out;
    for (int i = 0; i < upper.size(); i += 2) {
        if (!out.isEmpty()) {
            out += ':';
        }
        out += upper.mid(i, 2);
    }
    return out;
}
} // namespace

ConnectionLifecycle::ConnectionLifecycle(QObject* parent)
    : QObject(parent) {
}

void ConnectionLifecycle::manage(QWidget* window) {
    m_window = window;
}

void ConnectionLifecycle::setConnectionState(ConnectionManager::ConnectionState state) {
    if (m_connectionState == state) return;

    ConnectionManager::ConnectionState oldState = m_connectionState;
    m_connectionState = state;
    updateWindowTitle();

    if (state == ConnectionManager::Authenticated) {
        m_wasAuthenticated = true;
        m_authRetryPending = false;
    }

    // 重连/新连接时清除重试标记和错误码, 确保下次 AuthFailed 使用新的错误类型。
    // Connected 同样清除：同连接重试（AuthFailed → Connected）再次失败时，
    // 必须允许重新弹出凭据对话框（否则重入守卫会吞掉第二次 AuthFailed）
    if (state == ConnectionManager::Connecting || state == ConnectionManager::Connected) {
        m_authRetryPending = false;
        m_authErrorCode = ErrorCode::Unknown;
    }

    // AuthFailed 处理：可重试 → 弹凭据对话框；终态 → 关闭
    if (state == ConnectionManager::AuthFailed) {
        // 重入守卫：避免状态循环时重复调度对话框
        if (m_authRetryPending) return;

        if (m_authErrorCode == ErrorCode::AuthInvalidCredentials) {
            m_authRetryPending = true;
            // 服务端已统一失败响应（用户名/密码错误不可区分），用户名与密码均允许编辑。
            // 按值捕获错误消息，避免定时器触发前成员被覆写
            QTimer::singleShot(200, this, [this, msg = m_authErrorMessage]() {
                showCredentialDialog(msg, false);
            });
            return;
        }
    }

    // 终态处理：检查是否因可重试认证错误导致的断开，是则跳过关窗逻辑
    bool isTerminal = (state == ConnectionManager::Disconnected
                       || state == ConnectionManager::Error);
    bool wasActive = (oldState != ConnectionManager::Disconnecting);

    if (isTerminal && wasActive) {
        if (m_authRetryPending) {
            // 认证重试等待中，不关闭窗口
            return;
        }
        if (m_wasAuthenticated) {
            qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: Connection lost, scheduling disconnection dialog";

            QTimer::singleShot(100, this, [this]() {
                showDisconnectionDialog();
            });
        } else {
            qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: Pre-auth connection failed, closing window silently";

            QTimer::singleShot(100, this, [this]() {
                if (m_window) {
                    m_window->close();
                }
            });
        }
    }
}

void ConnectionLifecycle::setHostName(const QString& name) {
    if (!name.isEmpty()) {
        m_hostName = name;
        if (m_window) {
            m_window->setWindowTitle(name);
        }
    }
}

void ConnectionLifecycle::updateWindowTitle() {
    if (m_hostName.isEmpty() || !m_window) return;

    QString title;
    switch (m_connectionState) {
        case ConnectionManager::Connecting:
            title = tr("%1 - %2").arg(m_hostName, tr("正在连接..."));
            break;
        case ConnectionManager::VerifyingTrust:
            title = tr("%1 - %2").arg(m_hostName, tr("正在验证服务端身份..."));
            break;
        case ConnectionManager::Connected:
            title = tr("%1 - %2").arg(m_hostName, tr("已连接"));
            break;
        case ConnectionManager::Authenticated:
            title = tr("%1 - %2").arg(m_hostName, tr("已认证"));
            break;
        case ConnectionManager::Reconnecting:
            title = tr("%1 - %2").arg(m_hostName, tr("正在重连..."));
            break;
        case ConnectionManager::Disconnecting:
            title = tr("%1 - %2").arg(m_hostName, tr("正在断开连接..."));
            break;
        case ConnectionManager::Disconnected:
            title = tr("%1 - %2").arg(m_hostName, tr("未连接"));
            break;
        case ConnectionManager::Error:
            title = tr("%1 - %2").arg(m_hostName, tr("连接错误"));
            break;
        case ConnectionManager::AuthFailed:
            title = tr("%1 - %2").arg(m_hostName, tr("认证失败"));
            break;
        default:
            title = m_hostName;
            break;
    }
    if (m_viewOnly) {
        title = tr("%1 [仅查看]").arg(title);
    }
    m_window->setWindowTitle(title);
}

void ConnectionLifecycle::showDisconnectionDialog() {
    if (!m_window || m_dialogShowing) return;

    m_dialogShowing = true;

    qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: Showing disconnection dialog";

    QMessageBox msgBox(m_window);
    msgBox.setWindowTitle(tr("连接已断开"));
    msgBox.setText(tr("与远程主机 %1 的连接已断开。")
        .arg(m_hostName.isEmpty() ? tr("服务器") : m_hostName));
    msgBox.setInformativeText(tr("窗口即将关闭。"));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();

    qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: User confirmed disconnect, closing window";
    m_window->close();
}

void ConnectionLifecycle::setAuthErrorCode(ErrorCode code) {
    m_authErrorCode = code;
}

void ConnectionLifecycle::setAuthErrorMessage(const QString& msg) {
    m_authErrorMessage = msg;
}

void ConnectionLifecycle::setCachedUsername(const QString& name) {
    m_cachedUsername = name;
}

void ConnectionLifecycle::showCredentialDialog(const QString& errorMessage, bool passwordOnly) {
    if (!m_window) return;

    // 不使用 m_window 作为父对象——m_window 有 WA_DeleteOnClose，
    // 嵌套事件循环中若窗口关闭会导致 Qt 对栈对象调用 delete（未定义行为）
    QDialog dialog(nullptr);
    dialog.setWindowTitle(tr("认证失败"));
    dialog.setMinimumWidth(320);

    auto* layout = new QVBoxLayout(&dialog);

    // 错误信息（红色）
    auto* errorLabel = new QLabel(errorMessage);
    errorLabel->setStyleSheet("color: #d32f2f; font-weight: bold;");
    layout->addWidget(errorLabel);

    QLineEdit* usernameEdit = nullptr;
    if (passwordOnly) {
        // 仅密码错误：用户名只读展示
        auto* usernameLabel = new QLabel(tr("用户名: %1").arg(m_cachedUsername));
        layout->addWidget(usernameLabel);
    } else {
        // 用户名错误：可编辑
        layout->addWidget(new QLabel(tr("用户名:")));
        usernameEdit = new QLineEdit(m_cachedUsername);
        layout->addWidget(usernameEdit);
    }

    // 密码（始终可编辑）
    layout->addWidget(new QLabel(tr("密码:")));
    auto* passwordEdit = new QLineEdit();
    passwordEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(passwordEdit);

    // 按钮
    auto* buttonLayout = new QHBoxLayout();
    auto* retryBtn = new QPushButton(tr("重试"));
    auto* cancelBtn = new QPushButton(tr("取消"));
    buttonLayout->addStretch();
    buttonLayout->addWidget(retryBtn);
    buttonLayout->addWidget(cancelBtn);
    layout->addLayout(buttonLayout);

    QObject::connect(retryBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    // 布局完成后定位到窗口中心（adjustSize 确保 rect 为实际尺寸而非默认值）
    dialog.adjustSize();
    if (m_window->isVisible()) {
        dialog.move(m_window->geometry().center() - dialog.rect().center());
    }

    if (dialog.exec() == QDialog::Accepted) {
        QString newUsername = passwordOnly ? m_cachedUsername : usernameEdit->text().trimmed();
        QString newPassword = passwordEdit->text();
        m_cachedUsername = newUsername;  // 刷新缓存，供下次重试对话框预填
        emit retryAuthRequested(newUsername, newPassword);
    } else {
        m_authRetryPending = false;
        if (m_window) {
            m_window->close();
        }
    }
}

void ConnectionLifecycle::showTrustWarning(const QString& endpoint,
                                           const QString& oldFingerprint,
                                           const QString& newFingerprint) {
    if (!m_window) return;

    // 重入（挂起决策期间指纹再次变更）：替换对话框，呈现最新指纹
    if (m_trustDialog) {
        m_trustDialog->deleteLater();
        m_trustDialog = nullptr;
    }

    qCWarning(lcClientRemoteWindow) << "ConnectionLifecycle: 服务端身份变更，弹出信任警告" << endpoint;

    // 刻意非模态：不用 exec() 嵌套事件循环（模态会让 socket 事件在对话框栈帧"内部"
    // 被投递，窗口可能在对话框返回前被删除 → use-after-free，且重连产生的新指纹无法刷新对话框）。
    // parent 到窗口：窗口销毁时对话框随之删除；finished 以 this 为上下文连接，
    // this 随窗口销毁时连接自动断开——两条路径都不会触及已释放对象
    auto* dialog = new QDialog(m_window);
    m_trustDialog = dialog;
    dialog->setWindowTitle(tr("服务端身份已变更"));
    dialog->setMinimumWidth(420);

    auto* layout = new QVBoxLayout(dialog);

    auto* warnLabel = new QLabel(tr("警告：无法确认服务端身份"));
    warnLabel->setStyleSheet("color: #d32f2f; font-weight: bold;");
    layout->addWidget(warnLabel);

    layout->addWidget(new QLabel(tr(
        "主机 %1 的证书指纹与上次记录不一致。\n"
        "这可能是中间人攻击，也可能是服务端合法重装或更换证书。\n"
        "除非你确认服务端近期重装过，否则不要继续。").arg(endpoint)));

    layout->addWidget(new QLabel(tr("原指纹：\n%1").arg(formatFingerprint(oldFingerprint))));
    layout->addWidget(new QLabel(tr("新指纹：\n%1").arg(formatFingerprint(newFingerprint))));

    auto* buttonLayout = new QHBoxLayout();
    auto* trustBtn = new QPushButton(tr("信任新证书并连接"));
    auto* cancelBtn = new QPushButton(tr("取消"));
    buttonLayout->addStretch();
    buttonLayout->addWidget(trustBtn);
    buttonLayout->addWidget(cancelBtn);
    layout->addLayout(buttonLayout);

    QObject::connect(trustBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, dialog, &QDialog::reject);

    QObject::connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
        m_trustDialog = nullptr;
        dialog->deleteLater();
        const bool accept = (result == QDialog::Accepted);
        // 直连：ConnectionManager 同步处置（若已离开挂起态则忽略），
        // 返回后本对象缓存的状态即为最新
        emit trustDecision(accept);
        // 用户拒绝，或对话框期间连接已终结 → 关闭窗口（避免遗留无响应的僵尸窗口）
        if (!accept || m_connectionState == ConnectionManager::Disconnected
            || m_connectionState == ConnectionManager::Error) {
            if (m_window) {
                m_window->close();
            }
        }
    });

    dialog->adjustSize();
    if (m_window->isVisible()) {
        dialog->move(m_window->geometry().center() - dialog->rect().center());
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
