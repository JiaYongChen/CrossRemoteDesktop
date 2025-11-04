#include "ClientManager.h"
#include "./network/ConnectionManager.h"
#include "./managers/SessionManager.h"
#include "./window/ClientRemoteWindow.h"
#include "../common/core/config/UiConstants.h"
#include "../common/core/logging/LoggingCategories.h"
#include "../common/core/threading/ThreadManager.h"
#include "./network/TcpClient.h"  // 新增：获取实际服务器IP地址

#include <QtCore/QSettings>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QUuid>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QApplication>

// ConnectionInstance 方法实现

ConnectionInstance::~ConnectionInstance() {
    qCDebug(lcClientManager) << "~ConnectionInstance(): begin cleanup for" << connectionId;

    // 【修复段错误】在异常连接关闭时，确保安全的资源清理顺序
    try {
        // 1. 首先断开所有信号连接，防止在清理过程中触发回调
        if ( sessionManager && !sessionManager.isNull() ) {
            sessionManager->disconnectFromHost();
            sessionManager->disconnect();
        }
        if ( remoteDesktopWindow && !remoteDesktopWindow.isNull() ) {
            remoteDesktopWindow->disconnect();
        }

        // 2. 停止并清理 SessionManager 所在的线程
        if ( sessionThread ) {
            qCDebug(lcClientManager) << "~ConnectionInstance(): stopping session thread for" << connectionId;
            sessionThread->quit();
            if ( !sessionThread->wait(3000) ) {
                qCWarning(lcClientManager) << "~ConnectionInstance(): session thread did not stop in time, terminating";
                sessionThread->terminate();
                sessionThread->wait(1000);
            }
        }

        // 3. 按照创建的逆顺序删除对象
        if ( remoteDesktopWindow && !remoteDesktopWindow.isNull() ) {
            remoteDesktopWindow->close();
            remoteDesktopWindow->deleteLater();
            remoteDesktopWindow.clear();
        }

        if ( sessionManager && !sessionManager.isNull() ) {
            sessionManager->deleteLater();
            sessionManager.clear();
        }

        // 4. 删除线程对象
        if ( sessionThread ) {
            sessionThread->deleteLater();
            sessionThread = nullptr;
        }

        qCDebug(lcClientManager) << "~ConnectionInstance(): cleanup completed for" << connectionId;
    } catch ( const std::exception& e ) {
        qCWarning(lcClientManager) << "~ConnectionInstance(): exception during cleanup:" << e.what();
    } catch ( ... ) {
        qCWarning(lcClientManager) << "~ConnectionInstance(): unknown exception during cleanup";
    }
}

bool ConnectionInstance::isValid() const {
    return !connectionId.isEmpty() && !sessionManager.isNull();
}

QString ConnectionInstance::getConnectionState() const {
    if ( sessionManager.isNull() ) {
        return "Invalid";
    }

    // 使用 SessionManager 的方法
    if ( sessionManager->isAuthenticated() ) {
        return "Authenticated";
    } else if ( sessionManager->isConnected() ) {
        return "Connected";
    } else {
        return "Disconnected";
    }
}

QString ConnectionInstance::getHost() const {
    return sessionManager ? sessionManager->currentHost() : QString();
}

int ConnectionInstance::getPort() const {
    return sessionManager ? sessionManager->currentPort() : 0;
}

bool ConnectionInstance::isConnected() const {
    return sessionManager && sessionManager->isConnected();
}

bool ConnectionInstance::isAuthenticated() const {
    return sessionManager && sessionManager->isAuthenticated();
}

// ClientManager 方法实现

ClientManager::ClientManager(QObject* parent)
    : QObject(parent) {
}

ClientManager::~ClientManager() {
    qCDebug(lcClientManager) << "~ClientManager(): cleanupResources begin";
    cleanupResources();
    qCDebug(lcClientManager) << "~ClientManager(): cleanupResources end";
}

QString ClientManager::connectToHost(const QString& host, int port) {
    qCDebug(lcClientManager) << "connectToHost(): target" << host << ":" << port;
    // 生成新的连接ID并创建连接实例
    QString connectionId = generateConnectionId();
    ConnectionInstance* instance = new ConnectionInstance(connectionId);
    qCDebug(lcClientManager) << "connectToHost(): generated connectionId" << connectionId;

    // 创建 SessionManager 的独立线程
    instance->sessionThread = new QThread(this);
    instance->sessionThread->setObjectName(QString("SessionThread-%1").arg(connectionId));
    qCDebug(lcClientManager) << "connectToHost(): created session thread for" << connectionId;

    // 创建 SessionManager（不设置父对象，以便移动到线程）
    instance->sessionManager = new SessionManager(connectionId, nullptr);
    
    // 将 SessionManager 移动到独立线程
    instance->sessionManager->moveToThread(instance->sessionThread);
    qCDebug(lcClientManager) << "connectToHost(): moved SessionManager to independent thread";

    // 启动线程
    instance->sessionThread->start();
    qCDebug(lcClientManager) << "connectToHost(): session thread started";

    // 创建远程桌面窗口（必须在主线程中，因为它是 QWidget）
    instance->remoteDesktopWindow = createRemoteDesktopWindow(instance->sessionManager);
    if ( !instance->remoteDesktopWindow ) {
        qCWarning(lcClientManager) << "connectToHost(): failed to create remote desktop window";
        // 清理线程和 SessionManager
        instance->sessionThread->quit();
        instance->sessionThread->wait();
        delete instance->sessionManager;
        delete instance->sessionThread;
        delete instance;
        return QString();
    } else {
        qCDebug(lcClientManager) << "connectToHost(): created remote desktop window for" << connectionId;
        instance->remoteDesktopWindow->updateWindowTitle(host);
    }

    // 注册到连接表
    m_connections.insert(instance->connectionId, instance);

    // 使用 Qt::QueuedConnection 从主线程调用 SessionManager 的方法（跨线程调用）
    QMetaObject::invokeMethod(instance->sessionManager, 
        "connectToHost", 
        Qt::QueuedConnection,
        Q_ARG(QString, host),
        Q_ARG(int, port));

    qCDebug(lcClientManager) << "connectToHost(): connect request sent";
    return instance->connectionId;
}

void ClientManager::disconnectFromHost(const QString& connectionId) {
    qCDebug(lcClientManager) << "disconnectFromHost(): begin for" << connectionId;
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    if ( instance ) {
        if ( instance->sessionManager ) {
            // 使用 Qt::QueuedConnection 进行跨线程调用
            QMetaObject::invokeMethod(instance->sessionManager, 
                "disconnectFromHost", 
                Qt::QueuedConnection);
        }
        // 关闭远程桌面窗口
        if ( instance->remoteDesktopWindow ) {
            if ( !instance->remoteDesktopWindow->isClosing() ) {
                qCDebug(lcClientManager) << "disconnectFromHost(): request close for" << connectionId;
                instance->remoteDesktopWindow->close();
            } else {
                qCDebug(lcClientManager) << "disconnectFromHost(): window already closing" << connectionId;
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

void ClientManager::disconnectAll() {
    qCDebug(lcClientManager) << "disconnectAll(): begin, active count" << m_connections.size();
    QStringList connectionIds = m_connections.keys();
    for ( const QString& connectionId : connectionIds ) {
        disconnectFromHost(connectionId);
    }
    qCDebug(lcClientManager) << "disconnectAll(): end, remaining" << m_connections.size();
}

QStringList ClientManager::getActiveConnectionIds() const {
    QStringList activeIds;
    for ( auto it = m_connections.begin(); it != m_connections.end(); ++it ) {
        if ( it.value()->sessionManager && it.value()->sessionManager->isConnected() ) {
            activeIds.append(it.key());
        }
    }
    return activeIds;
}

int ClientManager::getActiveConnectionCount() const {
    return getActiveConnectionIds().count();
}

bool ClientManager::hasActiveConnections() const {
    return getActiveConnectionCount() > 0;
}

bool ClientManager::isConnected(const QString& connectionId) const {
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance && instance->isConnected();
}

bool ClientManager::isAuthenticated(const QString& connectionId) const {
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance && instance->isAuthenticated();
}

QString ClientManager::getCurrentHost(const QString& connectionId) const {
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->getHost() : QString();
}

int ClientManager::getCurrentPort(const QString& connectionId) const {
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->getPort() : 0;
}

SessionManager* ClientManager::sessionManager(const QString& connectionId) const {
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->sessionManager : nullptr;
}

ClientRemoteWindow* ClientManager::remoteDesktopWindow(const QString& connectionId) const {
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->remoteDesktopWindow : nullptr;
}

ClientRemoteWindow* ClientManager::createRemoteDesktopWindow(const SessionManager* sessionManager) {
    if ( !sessionManager ) {
        qCDebug(lcClientManager) << "createRemoteDesktopWindow(): invalid sessionManager";
        return nullptr;
    }

    // 直接传入 SessionManager，构造函数会自动设置
    ClientRemoteWindow* remoteDesktopWindow = new ClientRemoteWindow(const_cast<SessionManager*>(sessionManager), nullptr);

    remoteDesktopWindow->show();
    remoteDesktopWindow->raise();
    remoteDesktopWindow->activateWindow();

    // 强制处理事件循环，确保窗口立即显示
    QApplication::processEvents();

    // 连接窗口关闭信号
    connect(remoteDesktopWindow, &ClientRemoteWindow::windowClosed,
        this, &ClientManager::onWindowClosed);

    return remoteDesktopWindow;
}

void ClientManager::closeAllRemoteDesktopWindows() {
    for ( auto it = m_connections.begin(); it != m_connections.end(); ++it ) {
        if ( it.value()->remoteDesktopWindow ) {
            if ( !it.value()->remoteDesktopWindow->isClosing() ) {
                qCDebug(lcClientManager) << "closeAllRemoteDesktopWindows(): request close for" << it.key();
                it.value()->remoteDesktopWindow->close();
            } else {
                qCDebug(lcClientManager) << "closeAllRemoteDesktopWindows(): window already closing" << it.key();
            }
        }
    }
}

void ClientManager::onConnectionEstablished() {
    SessionManager* sessionManager = qobject_cast<SessionManager*>(sender());
    if ( !sessionManager ) {
        qCDebug(lcClientManager) << "onConnectionEstablished(): invalid sender";
        return;
    }

    qCDebug(lcClientManager) << "onConnectionEstablished(): for" << sessionManager->connectionId();
    emit connectionEstablished(sessionManager->connectionId());
}

void ClientManager::onAuthenticated() {
    SessionManager* sessionManager = qobject_cast<SessionManager*>(sender());
    if ( !sessionManager ) {
        qCDebug(lcClientManager) << "onAuthenticated(): invalid sender";
        return;
    }

    qCDebug(lcClientManager) << "onAuthenticated(): start session for" << sessionManager->connectionId();
    // 认证成功后启动会话
    sessionManager->startSession();
    // 连接屏幕更新信号
    connect(sessionManager, &SessionManager::screenUpdated,
        this, &ClientManager::onScreenUpdated);
}

void ClientManager::onConnectionClosed() {
    SessionManager* sessionManager = qobject_cast<SessionManager*>(sender());
    if ( !sessionManager ) {
        qCDebug(lcClientManager) << "onConnectionClosed(): invalid sender";
        return;
    }
    ConnectionInstance* instance = getConnectionInstance(sessionManager->connectionId());
    if ( instance && !instance->isBeingDeleted ) {
        qCDebug(lcClientManager) << "onConnectionClosed(): for" << instance->connectionId;

        // 设置删除标志，防止重复删除
        instance->isBeingDeleted = true;

        // 关闭远程桌面窗口
        if ( instance->remoteDesktopWindow ) {
            if ( !instance->remoteDesktopWindow->isClosing() ) {
                qCDebug(lcClientManager) << "onConnectionClosed(): request close for" << instance->connectionId;
                instance->remoteDesktopWindow->close();
            } else {
                qCDebug(lcClientManager) << "onConnectionClosed(): window already closing" << instance->connectionId;
            }
        }
        // 清理连接
        QString connectionId = instance->connectionId;
        cleanupConnection(instance);
        m_connections.remove(connectionId);
    }
}

void ClientManager::onConnectionError(const QString& error) {
    SessionManager* sessionManager = qobject_cast<SessionManager*>(sender());
    if ( !sessionManager ) {
        qCDebug(lcClientManager) << "onConnectionError(): invalid sender";
        return;
    }
    ConnectionInstance* instance = getConnectionInstance(sessionManager->connectionId());
    if ( instance && !instance->isBeingDeleted ) {
        qCDebug(lcClientManager) << "onConnectionError():" << error;

        // 设置删除标志，防止重复删除
        instance->isBeingDeleted = true;

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

void ClientManager::onScreenUpdated(const QPixmap& screen) {
    SessionManager* sessionManager = qobject_cast<SessionManager*>(sender());
    if ( !sessionManager ) {
        return;
    }

    ConnectionInstance* instance = getConnectionInstance(sessionManager->connectionId());
    if ( instance && instance->remoteDesktopWindow ) {
        instance->remoteDesktopWindow->updateRemoteScreen(screen);
    }
}

void ClientManager::onWindowClosed() {
    ClientRemoteWindow* window = qobject_cast<ClientRemoteWindow*>(sender());
    if ( !window ) {
        qCDebug(lcClientManager) << "onWindowClosed(): invalid sender";
        return;
    }

    ConnectionInstance* instance = getConnectionInstance(window->connectionId());
    if ( instance && !instance->isBeingDeleted ) {
        qCDebug(lcClientManager) << "onWindowClosed(): for" << instance->connectionId;

        // 设置删除标志，防止重复删除
        instance->isBeingDeleted = true;

        // 断开连接并清理资源
        if ( instance->sessionManager ) {
            instance->sessionManager->terminateSession();
            instance->sessionManager->disconnectFromHost();
        }

        // 从连接列表中移除
        m_connections.remove(instance->connectionId);

        // 清理连接实例（但不删除窗口，因为窗口正在关闭）
        if ( instance->sessionManager ) {
            instance->sessionManager->deleteLater();
            instance->sessionManager = nullptr;
        }

        delete instance;

        // 检查是否所有连接都已关闭，如果是则发射信号
        if ( m_connections.isEmpty() ) {
            qCDebug(lcClientManager) << "onWindowClosed(): all connections closed, emitting allConnectionsClosed signal";
            emit allConnectionsClosed();
        }
    } else if ( instance && instance->isBeingDeleted ) {
        qCDebug(lcClientManager) << "onWindowClosed(): instance already being deleted for" << instance->connectionId;
    }
}

void ClientManager::cleanupResources() {
    qCDebug(lcClientManager) << "cleanupResources(): begin, connections" << m_connections.size();

    // 【修复段错误】安全清理所有连接，防止重复删除
    try {
        // 创建连接实例的副本，避免在迭代过程中修改容器
        QList<ConnectionInstance*> instances;
        for ( auto it = m_connections.begin(); it != m_connections.end(); ++it ) {
            if ( it.value() && !it.value()->isBeingDeleted ) {
                it.value()->isBeingDeleted = true;  // 设置删除标志
                instances.append(it.value());
            }
        }

        // 清空连接映射
        m_connections.clear();

        // 安全删除所有连接实例
        for ( ConnectionInstance* instance : instances ) {
            cleanupConnection(instance);
        }

        qCDebug(lcClientManager) << "cleanupResources(): end, cleaned" << instances.size() << "connections";
    } catch ( const std::exception& e ) {
        qCWarning(lcClientManager) << "cleanupResources(): exception during cleanup:" << e.what();
    } catch ( ... ) {
        qCWarning(lcClientManager) << "cleanupResources(): unknown exception during cleanup";
    }
}

void ClientManager::cleanupConnection(ConnectionInstance* instance) {
    if ( !instance ) {
        return;
    }

    // ConnectionInstance的析构函数会自动处理资源清理
    delete instance;
}

QString ClientManager::generateConnectionId() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

ConnectionInstance* ClientManager::getConnectionInstance(const QString& connectionId) const {
    return m_connections.value(connectionId, nullptr);
}