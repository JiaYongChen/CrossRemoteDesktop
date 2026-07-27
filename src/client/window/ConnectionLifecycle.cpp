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

    // 记录是否曾拥有真实会话——后续以此区分"会话丢失"和"初始连接失败"
    if (state == ConnectionManager::Authenticated) {
        m_wasAuthenticated = true;
        m_authRetryPending = false;
    }

    // AuthFailed 处理：可重试 → 弹凭据对话框；终态 → 关闭
    if (state == ConnectionManager::AuthFailed) {
        if (!m_authErrorMessage.isEmpty()) {
            bool isPasswordError = m_authErrorMessage.contains(tr("密码错误"));
            bool isUsernameError = m_authErrorMessage.contains(tr("用户名无效"));
            if (isPasswordError || isUsernameError) {
                m_authRetryPending = true;
                m_pendingAuthError = m_authErrorMessage;
                bool passwordOnly = isPasswordError;  // 密码错误→仅重输密码
                QTimer::singleShot(200, this, [this, passwordOnly]() {
                    showCredentialDialog(m_pendingAuthError, passwordOnly);
                });
                return;
            }
        }
    }

    // 判定是否到达终态且需要通知用户。
    // 条件：1) 曾认证（真实会话丢失，而非初始连接失败）
    //       2) 当前为终态（Disconnected / Error）
    //       3) 非用户主动断开（Disconnecting）
    bool isTerminal = (state == ConnectionManager::Disconnected
                       || state == ConnectionManager::Error);
    bool wasActive = (oldState != ConnectionManager::Disconnecting);

    if (isTerminal && wasActive) {
        if (m_wasAuthenticated) {
            // 曾拥有真实会话 → 弹窗告知用户会话丢失，确认后关闭窗口
            qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: Connection lost, scheduling disconnection dialog";

            QTimer::singleShot(100, this, [this]() {
                showDisconnectionDialog();
            });
        } else {
            // 从未认证 → 连接从未建立完成，静默关闭窗口（无需误导性弹窗）
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
    msgBox.setWindowTitle(tr("Connection Disconnected"));
    msgBox.setText(tr("Connection to remote host %1 has been disconnected.")
        .arg(m_hostName.isEmpty() ? "Server" : m_hostName));
    msgBox.setInformativeText(tr("The window will close."));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();

    qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: User confirmed disconnect, closing window";
    m_window->close();
}

void ConnectionLifecycle::setConnectionManager(ConnectionManager* mgr) {
    m_connectionManager = mgr;
}

void ConnectionLifecycle::setAuthErrorMessage(const QString& msg) {
    m_authErrorMessage = msg;
}

void ConnectionLifecycle::setCachedUsername(const QString& name) {
    m_cachedUsername = name;
}

void ConnectionLifecycle::showCredentialDialog(const QString& errorMessage, bool passwordOnly) {
    if (!m_window) return;

    QDialog dialog(m_window);
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

    // 密码（始终可编辑，清空让用户重新输入）
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

    if (dialog.exec() == QDialog::Accepted) {
        QString newUsername = passwordOnly ? m_cachedUsername : usernameEdit->text().trimmed();
        QString newPassword = passwordEdit->text();
        emit retryAuthRequested(newUsername, newPassword);
    } else {
        m_authRetryPending = false;
        if (m_window) {
            m_window->close();
        }
    }
}
