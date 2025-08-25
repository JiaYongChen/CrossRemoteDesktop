#include "clientmanager.h"
#include "./managers/connectionmanager.h"
#include "./managers/sessionmanager.h"
#include "clientremotewindow.h"
#include "../core/uiconstants.h"
#include "../../common/core/logging_categories.h"

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
        remoteDesktopWindow->close();
        remoteDesktopWindow->deleteLater();
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
    cleanupResources();
}

QString ClientManager::connectToHost(const QString &host, int port)
{
    qDebug() << "[ClientManager] connectToHost called for" << host << ":" << port;
    
    // 创建新的连接实例
    QString connectionId = generateConnectionId();
    ConnectionInstance* instance = new ConnectionInstance(connectionId);
    
    qDebug() << "[ClientManager] Generated connectionId:" << connectionId;
    
    // 创建连接管理器
    instance->connectionManager = new ConnectionManager(this);
    
    // 从设置读取连接参数并应用到 ConnectionManager（秒 -> 毫秒）
    {
        QSettings settings;
        settings.beginGroup("Connection");
        const int timeoutSec = settings.value("connectionTimeout", 30).toInt();
        const bool autoReconnect = settings.value("autoReconnect", false).toBool();
        const int reconnectIntervalSec = settings.value("reconnectInterval", 5).toInt();
        const int maxReconnectAttempts = settings.value("maxReconnectAttempts", 3).toInt();
        settings.endGroup();
    
        // 应用到连接管理器
        instance->connectionManager->setConnectionTimeout(timeoutSec * 1000);
        instance->connectionManager->setAutoReconnect(autoReconnect);
        instance->connectionManager->setReconnectInterval(reconnectIntervalSec * 1000);
        instance->connectionManager->setMaxReconnectAttempts(maxReconnectAttempts);
    }
    
    // 创建会话管理器
    instance->sessionManager = new SessionManager(instance->connectionManager, this);

    // 存储连接实例（必须在创建窗口之前）
    m_connections.insert(instance->connectionId, instance);
    
    // 创建远程桌面窗口
    createRemoteDesktopWindow(instance->connectionId);
    
    // 设置连接
    setupConnections(instance);
    
    // 发起连接
    instance->connectionManager->connectToHost(host, port);
    
    return instance->connectionId;
}

void ClientManager::disconnectFromHost(const QString &connectionId)
{
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    if (instance && instance->connectionManager) {
        instance->connectionManager->disconnectFromHost();
        
        // 关闭远程桌面窗口
        if (instance->remoteDesktopWindow) {
            instance->remoteDesktopWindow->close();
        }
        
        // 清理连接
        cleanupConnection(instance);
        m_connections.remove(connectionId);
    }
}

void ClientManager::disconnectAll()
{
    QStringList connectionIds = m_connections.keys();
    for (const QString &connectionId : connectionIds) {
        disconnectFromHost(connectionId);
    }
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
        return;
    }
    
    // 创建远程桌面窗口
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
            it.value()->remoteDesktopWindow->close();
        }
    }
}

void ClientManager::onConnectionEstablished()
{
    ConnectionManager* manager = qobject_cast<ConnectionManager*>(sender());
    if (!manager) {
        return;
    }
    
    ConnectionInstance* instance = findConnectionByManager(manager);
    if (instance) {
        emit connectionEstablished(instance->connectionId);
    }
}

void ClientManager::onAuthenticated()
{
    ConnectionManager* manager = qobject_cast<ConnectionManager*>(sender());
    if (!manager) {
        return;
    }
    
    ConnectionInstance* instance = findConnectionByManager(manager);
    if (!instance) {
        return;
    }
    
    if (instance->sessionManager) {
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
        return;
    }
    
    ConnectionInstance* instance = findConnectionByManager(manager);
    if (instance) {
        // 关闭远程桌面窗口
        if (instance->remoteDesktopWindow) {
            instance->remoteDesktopWindow->close();
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
        return;
    }
    
    ConnectionInstance* instance = findConnectionByManager(manager);
    if (instance) {
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
    // 清理所有连接
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        cleanupConnection(it.value());
    }
    m_connections.clear();
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