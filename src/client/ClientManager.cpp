#include "ClientManager.h"
#include "./managers/ConnectionManager.h"
#include "./managers/SessionManager.h"
#include "ClientRemoteWindow.h"
#include "../common/core/config/UiConstants.h"
#include "../../common/core/logging/LoggingCategories.h"
#include "TcpClient.h"  // 新增：获取实际服务器IP地址

#include <QtCore/QSettings>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QUuid>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QApplication>

// ConnectionInstance 方法实现

ConnectionInstance::~ConnectionInstance()
{
    // QPointer会自动处理对象的安全访问，但我们仍需要确保正确的清理顺序
    if (remoteDesktopWindow) {
        // 若窗口未进入关闭流程，则触发关闭；否则跳过，避免重入
        if (!remoteDesktopWindow->isClosing()) {
            qCDebug(lcClientManager, "~ConnectionInstance(): request close for %s", qPrintable(connectionId));
            remoteDesktopWindow->close();
        } else {
            qCDebug(lcClientManager, "~ConnectionInstance(): window already closing %s", qPrintable(connectionId));
        }
        // 统一由onWindowClosed回调与cleanupConnection()负责最终清理，因此此处不直接deleteLater
    }
    
    if (sessionManager) {
        sessionManager->deleteLater();
    }
    
    if (connectionManager) {
        connectionManager->deleteLater();
    }
}

bool ConnectionInstance::isValid() const
{
    return !connectionId.isEmpty() && 
           !connectionManager.isNull() && 
           !sessionManager.isNull();
}

QString ConnectionInstance::getConnectionState() const
{
    if (connectionManager.isNull()) {
        return "Invalid";
    }
    
    switch (connectionManager->connectionState()) {
        case ConnectionManager::Connecting: return "Connecting";
        case ConnectionManager::Connected: return "Connected";
        case ConnectionManager::Authenticating: return "Authenticating";
        case ConnectionManager::Authenticated: return "Authenticated";
        case ConnectionManager::Reconnecting: return "Reconnecting";
        case ConnectionManager::Disconnecting: return "Disconnecting";
        case ConnectionManager::Disconnected: return "Disconnected";
        case ConnectionManager::Error: return "Error";
        default: return "Unknown";
    }
}

QString ConnectionInstance::getHost() const
{
    return connectionManager.isNull() ? QString() : connectionManager->currentHost();
}

int ConnectionInstance::getPort() const
{
    return connectionManager.isNull() ? 0 : connectionManager->currentPort();
}

bool ConnectionInstance::isConnected() const
{
    return !connectionManager.isNull() && connectionManager->isConnected();
}

bool ConnectionInstance::isAuthenticated() const
{
    return !connectionManager.isNull() && connectionManager->isAuthenticated();
}

// ClientManager 方法实现

ClientManager::ClientManager(QObject *parent)
    : QObject(parent)
{
}

ClientManager::~ClientManager()
{
    qCDebug(lcClientManager) << "~ClientManager(): cleanupResources begin";
    cleanupResources();
    qCDebug(lcClientManager) << "~ClientManager(): cleanupResources end";
}

QString ClientManager::connectToHost(const QString &host, int port)
{
    qCDebug(lcClientManager) << "connectToHost(): target" << host << ":" << port;
    // 生成新的连接ID并创建连接实例
    QString connectionId = generateConnectionId();
    ConnectionInstance* instance = new ConnectionInstance(connectionId);
    qCDebug(lcClientManager) << "connectToHost(): generated connectionId" << connectionId;
    
    // 建立组件（ConnectionManager / SessionManager 等）
    instance->connectionManager = new ConnectionManager(this);
    instance->sessionManager = new SessionManager(instance->connectionManager, this);
    
    // 注册到连接表
    m_connections.insert(instance->connectionId, instance);
    
    // 创建远程桌面窗口
    createRemoteDesktopWindow(instance->connectionId);
    
    // 新增：将 host 传递给远程桌面窗口，用于仅显示 IP/主机名的窗口标题
    if (instance->remoteDesktopWindow) {
        instance->remoteDesktopWindow->setConnectionHost(host);
    }
    
    // 建立内部信号连接
    setupConnections(instance);
    
    // 发起网络连接
    instance->connectionManager->connectToHost(host, port);
    qCDebug(lcClientManager) << "connectToHost(): connect request sent";
    return instance->connectionId;
}

void ClientManager::disconnectFromHost(const QString &connectionId)
{
    qCDebug(lcClientManager) << "disconnectFromHost(): begin for" << connectionId;
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    if (instance && instance->connectionManager) {
        instance->connectionManager->disconnectFromHost();
        // 关闭远程桌面窗口
        if (instance->remoteDesktopWindow) {
            if (!instance->remoteDesktopWindow->isClosing()) {
                qCDebug(lcClientManager, "disconnectFromHost(): request close for %s", qPrintable(connectionId));
                instance->remoteDesktopWindow->close();
            } else {
                qCDebug(lcClientManager, "disconnectFromHost(): window already closing %s", qPrintable(connectionId));
            }
        }
        // 清理连接
        cleanupConnection(instance);
        m_connections.remove(connectionId);
        qCDebug(lcClientManager) << "disconnectFromHost(): end for" << connectionId;
    } else {
        qCDebug(lcClientManager) << "disconnectFromHost(): no instance for" << connectionId;
    }
}

void ClientManager::disconnectAll()
{
    qCDebug(lcClientManager) << "disconnectAll(): begin, active count" << m_connections.size();
    QStringList connectionIds = m_connections.keys();
    for (const QString &connectionId : connectionIds) {
        disconnectFromHost(connectionId);
    }
    qCDebug(lcClientManager) << "disconnectAll(): end, remaining" << m_connections.size();
}

QStringList ClientManager::getActiveConnectionIds() const
{
    QStringList activeIds;
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        if (it.value()->connectionManager && it.value()->connectionManager->isConnected()) {
            activeIds.append(it.key());
        }
    }
    return activeIds;
}

int ClientManager::getActiveConnectionCount() const
{
    return getActiveConnectionIds().count();
}

bool ClientManager::hasActiveConnections() const
{
    return getActiveConnectionCount() > 0;
}

bool ClientManager::isConnected(const QString &connectionId) const
{
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance && instance->connectionManager && instance->connectionManager->isConnected();
}

bool ClientManager::isAuthenticated(const QString &connectionId) const
{
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance && instance->connectionManager && instance->connectionManager->isAuthenticated();
}

QString ClientManager::getCurrentHost(const QString &connectionId) const
{
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->getHost() : QString();
}

int ClientManager::getCurrentPort(const QString &connectionId) const
{
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->getPort() : 0;
}

ConnectionManager* ClientManager::connectionManager(const QString &connectionId) const
{
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->connectionManager : nullptr;
}

SessionManager* ClientManager::sessionManager(const QString &connectionId) const
{
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->sessionManager : nullptr;
}

ClientRemoteWindow* ClientManager::remoteDesktopWindow(const QString &connectionId) const
{
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->remoteDesktopWindow : nullptr;
}

void ClientManager::createRemoteDesktopWindow(const QString &connectionId)
{
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    if (!instance || instance->remoteDesktopWindow) {
        qCDebug(lcClientManager) << "createRemoteDesktopWindow(): skip, window exists or invalid instance for" << connectionId;
        return;
    }
    qCDebug(lcClientManager) << "createRemoteDesktopWindow(): create window for" << connectionId;
    instance->remoteDesktopWindow = new ClientRemoteWindow(connectionId, nullptr);
    
    // 设置SessionManager
    if (instance->sessionManager) {
        instance->remoteDesktopWindow->setSessionManager(instance->sessionManager);
    }
    
    instance->remoteDesktopWindow->show();
    instance->remoteDesktopWindow->raise();
    instance->remoteDesktopWindow->activateWindow();
    
    // 强制处理事件循环，确保窗口立即显示
    QApplication::processEvents();
    
    // 连接窗口关闭信号
    connect(instance->remoteDesktopWindow, &ClientRemoteWindow::windowClosed,
            this, &ClientManager::onWindowClosed);
}

void ClientManager::closeAllRemoteDesktopWindows()
{
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        if (it.value()->remoteDesktopWindow) {
            if (!it.value()->remoteDesktopWindow->isClosing()) {
                qCDebug(lcClientManager, "closeAllRemoteDesktopWindows(): request close for %s", qPrintable(it.key()));
                it.value()->remoteDesktopWindow->close();
            } else {
                qCDebug(lcClientManager, "closeAllRemoteDesktopWindows(): window already closing %s", qPrintable(it.key()));
            }
        }
    }
}

void ClientManager::onConnectionEstablished()
{
    ConnectionManager* manager = qobject_cast<ConnectionManager*>(sender());
    if (!manager) {
        qCDebug(lcClientManager) << "onConnectionEstablished(): invalid sender";
        return;
    }
    ConnectionInstance* instance = findConnectionByManager(manager);
    if (instance) {
        qCDebug(lcClientManager) << "onConnectionEstablished(): for" << instance->connectionId;
        // 新增：连接建立后，用真实的服务器IP更新窗口标题，确保仅显示IP
        if (instance->remoteDesktopWindow) {
            QString ip;
            if (TcpClient* tcp = manager->tcpClient()) {
                ip = tcp->serverAddress();  // 直接获取对端IP（字符串）
            }
            if (ip.isEmpty()) {
                ip = manager->currentHost();  // 兜底使用当前主机名/地址
            }
            if (!ip.isEmpty()) {
                qCDebug(lcClientManager) << "onConnectionEstablished(): update window title to IP" << ip;
                instance->remoteDesktopWindow->setConnectionHost(ip);
            }
        }
        emit connectionEstablished(instance->connectionId);
    } else {
        qCDebug(lcClientManager) << "onConnectionEstablished(): instance not found";
    }
}

void ClientManager::onAuthenticated()
{
    ConnectionManager* manager = qobject_cast<ConnectionManager*>(sender());
    if (!manager) {
        qCDebug(lcClientManager) << "onAuthenticated(): invalid sender";
        return;
    }
    ConnectionInstance* instance = findConnectionByManager(manager);
    if (!instance) {
        qCDebug(lcClientManager) << "onAuthenticated(): instance not found";
        return;
    }
    if (instance->sessionManager) {
        qCDebug(lcClientManager) << "onAuthenticated(): start session for" << instance->connectionId;
        // 认证成功后启动会话
        instance->sessionManager->startSession();
        // 连接屏幕更新信号
        connect(instance->sessionManager, &SessionManager::screenUpdated,
                this, &ClientManager::onScreenUpdated);
    }
}

void ClientManager::onConnectionClosed()
{
    ConnectionManager* manager = qobject_cast<ConnectionManager*>(sender());
    if (!manager) {
        qCDebug(lcClientManager) << "onConnectionClosed(): invalid sender";
        return;
    }
    ConnectionInstance* instance = findConnectionByManager(manager);
    if (instance) {
        qCDebug(lcClientManager) << "onConnectionClosed(): for" << instance->connectionId;
        // 关闭远程桌面窗口
        if (instance->remoteDesktopWindow) {
            if (!instance->remoteDesktopWindow->isClosing()) {
                qCDebug(lcClientManager, "onConnectionClosed(): request close for %s", qPrintable(instance->connectionId));
                instance->remoteDesktopWindow->close();
            } else {
                qCDebug(lcClientManager, "onConnectionClosed(): window already closing %s", qPrintable(instance->connectionId));
            }
        }
        // 清理连接
        QString connectionId = instance->connectionId;
        cleanupConnection(instance);
        m_connections.remove(connectionId);
    }
}

void ClientManager::onConnectionError(const QString &error)
{
    ConnectionManager* manager = qobject_cast<ConnectionManager*>(sender());
    if (!manager) {
        qCDebug(lcClientManager) << "onConnectionError(): invalid sender";
        return;
    }
    ConnectionInstance* instance = findConnectionByManager(manager);
    if (instance) {
        qCDebug(lcClientManager) << "onConnectionError():" << error;
        QMessageBox msgBox(instance->remoteDesktopWindow);
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowTitle(tr("服务器错误"));
        msgBox.setText(tr("连接服务器时发生错误：%1").arg(error));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        cleanupConnection(instance);
        m_connections.remove(instance->connectionId);
    }
}

void ClientManager::onSessionStateChanged()
{
    SessionManager* sessionManager = qobject_cast<SessionManager*>(sender());
    if (!sessionManager) {
        return;
    }
}

void ClientManager::onConnectionStateChanged(ConnectionManager::ConnectionState state)
{
    ConnectionManager* manager = qobject_cast<ConnectionManager*>(sender());
    if (!manager) {
        qCDebug(lcClientManager) << "onConnectionStateChanged(): invalid sender";
        return;
    }
    ConnectionInstance* instance = findConnectionByManager(manager);
    if (instance) {        
        // 根据连接状态更新UI
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << "ClientManager: Connection state changed for" << instance->connectionId << "to" << state;
        if (instance->remoteDesktopWindow) {
            instance->remoteDesktopWindow->setConnectionState(state);
        }
    }
}

void ClientManager::onScreenUpdated(const QPixmap &screen)
{
    SessionManager* sessionManager = qobject_cast<SessionManager*>(sender());
    if (!sessionManager) {
        return;
    }
    
    ConnectionInstance* instance = findConnectionBySessionManager(sessionManager);
    if (instance && instance->remoteDesktopWindow) {
        instance->remoteDesktopWindow->updateRemoteScreen(screen);
    }
}

void ClientManager::onWindowClosed()
{
    ClientRemoteWindow* window = qobject_cast<ClientRemoteWindow*>(sender());
    if (!window) {
        qCDebug(lcClientManager) << "onWindowClosed(): invalid sender";
        return;
    }
    
    ConnectionInstance* instance = findConnectionByWindow(window);
    if (instance) {
        // 断开连接并清理资源
        if (instance->connectionManager) {
            instance->connectionManager->disconnectFromHost();
        }
        
        // 从连接列表中移除
        m_connections.remove(instance->connectionId);
        
        // 清理连接实例（但不删除窗口，因为窗口正在关闭）
        if (instance->sessionManager) {
            instance->sessionManager->deleteLater();
            instance->sessionManager = nullptr;
        }
        
        if (instance->connectionManager) {
            instance->connectionManager->deleteLater();
            instance->connectionManager = nullptr;
        }
        
        delete instance;
    }
}

void ClientManager::setupConnections(ConnectionInstance* instance)
{
    if (!instance || !instance->connectionManager || !instance->sessionManager) {
        return;
    }
    
    // 连接管理器信号
    connect(instance->connectionManager, &ConnectionManager::connected,
            this, &ClientManager::onConnectionEstablished);
    connect(instance->connectionManager, &ConnectionManager::disconnected,
            this, &ClientManager::onConnectionClosed);
    connect(instance->connectionManager, &ConnectionManager::errorOccurred,
            this, &ClientManager::onConnectionError);
    connect(instance->connectionManager, &ConnectionManager::authenticated,
            this, &ClientManager::onAuthenticated);
    connect(instance->connectionManager, &ConnectionManager::connectionStateChanged,
            this, &ClientManager::onConnectionStateChanged);
    
    // 会话管理器信号
    connect(instance->sessionManager, &SessionManager::sessionStateChanged,
            this, &ClientManager::onSessionStateChanged);
}

void ClientManager::cleanupResources()
{
    qCDebug(lcClientManager) << "cleanupResources(): begin, connections" << m_connections.size();
    // 清理所有连接
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        cleanupConnection(it.value());
    }
    m_connections.clear();
    qCDebug(lcClientManager) << "cleanupResources(): end";
}

void ClientManager::cleanupConnection(ConnectionInstance* instance)
{
    if (!instance) {
        return;
    }
    
    // ConnectionInstance的析构函数会自动处理资源清理
    delete instance;
}

QString ClientManager::generateConnectionId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

ConnectionInstance* ClientManager::getConnectionInstance(const QString &connectionId) const
{
    return m_connections.value(connectionId, nullptr);
}

ConnectionInstance* ClientManager::findConnectionByManager(ConnectionManager* manager) const
{
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        if (it.value()->connectionManager == manager) {
            return it.value();
        }
    }
    return nullptr;
}

ConnectionInstance* ClientManager::findConnectionBySessionManager(SessionManager* sessionManager) const
{
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        if (it.value()->sessionManager == sessionManager) {
            return it.value();
        }
    }
    return nullptr;
}

ConnectionInstance* ClientManager::findConnectionByWindow(ClientRemoteWindow* window) const
{
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        if (it.value()->remoteDesktopWindow == window) {
            return it.value();
        }
    }
    return nullptr;
}