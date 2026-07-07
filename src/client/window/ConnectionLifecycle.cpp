#include "ConnectionLifecycle.h"
#include "../../common/core/config/MessageConstants.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtWidgets/QMessageBox>
#include <QtCore/QTimer>

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

    if (state == ConnectionManager::Disconnected) {
        if (oldState == ConnectionManager::Connected ||
            oldState == ConnectionManager::Authenticated ||
            oldState == ConnectionManager::Authenticating ||
            oldState == ConnectionManager::Error) {

            qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: Connection lost, scheduling disconnection dialog";

            // Post to event loop to avoid re-entrancy from signal handlers
            QTimer::singleShot(100, this, [this]() {
                showDisconnectionDialog();
            });
        }
    }
}

ConnectionManager::ConnectionState ConnectionLifecycle::connectionState() const {
    return m_connectionState;
}

void ConnectionLifecycle::setHostName(const QString& name) {
    if (!name.isEmpty()) {
        m_hostName = name;
        if (m_window) {
            m_window->setWindowTitle(name);
        }
    }
}

QString ConnectionLifecycle::hostName() const {
    return m_hostName;
}

void ConnectionLifecycle::updateWindowTitle() {
    if (m_hostName.isEmpty() || !m_window) return;

    QString title;
    switch (m_connectionState) {
        case ConnectionManager::Connecting:
            title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_CONNECTING);
            break;
        case ConnectionManager::Connected:
            title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_CONNECTED);
            break;
        case ConnectionManager::Authenticating:
            title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_AUTHENTICATING);
            break;
        case ConnectionManager::Authenticated:
            title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_AUTHENTICATED);
            break;
        case ConnectionManager::Disconnecting:
            title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_DISCONNECTING);
            break;
        case ConnectionManager::Disconnected:
            title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_DISCONNECTED);
            break;
        case ConnectionManager::Reconnecting:
            title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_RECONNECTING);
            break;
        case ConnectionManager::Error:
            title = tr("%1 - %2").arg(m_hostName).arg(MessageConstants::UI::STATUS_ERROR);
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
