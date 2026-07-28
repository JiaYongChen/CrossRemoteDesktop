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

    // 重连/新连接时清除重试标记和错误码, 确保下次 AuthFailed 使用新的错误类型
    if (state == ConnectionManager::Connecting) {
        m_authRetryPending = false;
        m_authErrorCode = ErrorCode::Unknown;
    }

    // AuthFailed 处理：可重试 → 弹凭据对话框；终态 → 关闭
    if (state == ConnectionManager::AuthFailed) {
        // 重入守卫：避免状态循环时重复调度对话框
        if (m_authRetryPending) return;

        if (m_authErrorCode == ErrorCode::AuthInvalidUsername
            || m_authErrorCode == ErrorCode::AuthInvalidPassword) {
            m_authRetryPending = true;
            bool passwordOnly = (m_authErrorCode == ErrorCode::AuthInvalidPassword);
            // 按值捕获错误消息，避免定时器触发前成员被覆写
            QTimer::singleShot(200, this, [this, passwordOnly, msg = m_authErrorMessage]() {
                showCredentialDialog(msg, passwordOnly);
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
