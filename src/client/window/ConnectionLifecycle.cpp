#include "ConnectionLifecycle.h"

#include <QtCore/QTimer>
#include <QtWidgets/QMessageBox>

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
