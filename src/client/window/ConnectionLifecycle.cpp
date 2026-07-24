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

    // 仅在真正"终态断开"时弹对话框（AuthFailed/Reconnecting 不弹——
    // AuthFailed 已有专属错误信号，Reconnecting 可能恢复）
    if (state == ConnectionManager::Disconnected) {
        if (oldState == ConnectionManager::Connected ||
            oldState == ConnectionManager::Authenticated ||
            oldState == ConnectionManager::Error) {

            qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: Connection lost, scheduling disconnection dialog";

            // Post to event loop to avoid re-entrancy from signal handlers
            QTimer::singleShot(100, this, [this]() {
                showDisconnectionDialog();
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
    if (!m_window) return;

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
