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
    qCInfo(lcClientManager) << "~ConnectionInstance(): [START] Cleanup for connection:" << connectionId;

    // 【优化析构流程】确保线程安全和正确的资源释放顺序
    try {
        // 第一阶段：关闭窗口和断开连接（保持信号连接以便正常清理）
        if ( remoteDesktopWindow && !remoteDesktopWindow.isNull() ) {
            // 阻塞关闭窗口，确保窗口完全关闭后再继续
            if ( !remoteDesktopWindow->isClosing() ) {
                qCDebug(lcClientManager) << "~ConnectionInstance(): [PHASE-1] Closing remote window for" << connectionId;
                remoteDesktopWindow->close();
            } else {
                qCDebug(lcClientManager) << "~ConnectionInstance(): [PHASE-1] Window already closing for" << connectionId;
            }
            // 断开窗口信号，防止关闭过程中触发新的事件
            remoteDesktopWindow->disconnect();
        }

        if ( sessionManager && !sessionManager.isNull() ) {
            // 使用 BlockingQueuedConnection 确保断开操作完成（带超时保护）
            qCDebug(lcClientManager) << "~ConnectionInstance(): [PHASE-1] Disconnecting session for" << connectionId;
            bool invoked = QMetaObject::invokeMethod(sessionManager, "disconnectFromHost", 
                Qt::BlockingQueuedConnection);
            if (!invoked) {
                qCWarning(lcClientManager) << "~ConnectionInstance(): [PHASE-1] Failed to invoke disconnectFromHost for" << connectionId;
            }
        }

        // 第二阶段：优雅地停止线程（等待 SessionManager 完成当前操作）
        if ( instanceThread && instanceThread->isRunning() ) {
            qCDebug(lcClientManager) << "~ConnectionInstance(): [PHASE-2] Stopping session thread for" << connectionId;

            // 先请求线程退出
            instanceThread->quit();

            // 等待线程正常退出（使用配置的超时时间）
            if ( !instanceThread->wait(THREAD_QUIT_TIMEOUT_MS) ) {
                qCWarning(lcClientManager) << "~ConnectionInstance(): [PHASE-2] Thread quit timeout after" 
                    << THREAD_QUIT_TIMEOUT_MS << "ms, force terminating for" << connectionId;
                // 强制终止线程
                instanceThread->terminate();
                // 再次等待确保线程已终止
                if ( !instanceThread->wait(THREAD_TERMINATE_TIMEOUT_MS) ) {
                    qCCritical(lcClientManager) << "~ConnectionInstance(): [PHASE-2] Thread terminate failed after" 
                        << THREAD_TERMINATE_TIMEOUT_MS << "ms for" << connectionId;
                }
            } else {
                qCDebug(lcClientManager) << "~ConnectionInstance(): [PHASE-2] Thread stopped gracefully for" << connectionId;
            }
        } else if ( instanceThread ) {
            qCDebug(lcClientManager) << "~ConnectionInstance(): [PHASE-2] Thread already stopped for" << connectionId;
        }

        // 第三阶段：删除 SessionManager（线程已停止，安全删除）
        if ( sessionManager && !sessionManager.isNull() ) {
            qCDebug(lcClientManager) << "~ConnectionInstance(): [PHASE-3] Deleting SessionManager for" << connectionId;
            // 断开所有信号连接
            sessionManager->disconnect();
            // 直接删除而非 deleteLater，因为线程已停止
            delete sessionManager.data();
            // QPointer 会自动清空，无需手动 clear()
            qCDebug(lcClientManager) << "~ConnectionInstance(): [PHASE-3] SessionManager deleted for" << connectionId;
        }

        // 第四阶段：删除窗口对象（在主线程中）
        if ( remoteDesktopWindow && !remoteDesktopWindow.isNull() ) {
            qCDebug(lcClientManager) << "~ConnectionInstance(): [PHASE-4] Scheduling window deletion for" << connectionId;
            // 使用 deleteLater 因为窗口必须在主线程的事件循环中删除
            remoteDesktopWindow->deleteLater();
            // QPointer 会自动清空
        }

        // 第五阶段：删除线程对象
        if ( instanceThread ) {
            // 确保线程已经完全停止
            if ( instanceThread->isRunning() ) {
                qCWarning(lcClientManager) << "~ConnectionInstance(): [PHASE-5] Thread still running, waiting again for" << connectionId;
                instanceThread->quit();
                instanceThread->wait(THREAD_QUIT_TIMEOUT_MS);
            }
            
            qCDebug(lcClientManager) << "~ConnectionInstance(): [PHASE-5] Deleting thread object for" << connectionId;
            // 线程已停止，可以安全删除
            delete instanceThread;
            instanceThread = nullptr;
        }

        qCInfo(lcClientManager) << "~ConnectionInstance(): [COMPLETE] Cleanup completed successfully for" << connectionId;
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
    : QObject(parent)
    , m_screenUpdateTimer(new QTimer(this)) {
    // 设置定时器，每30ms更新一次屏幕（约33fps）
    m_screenUpdateTimer->setInterval(30);
    connect(m_screenUpdateTimer, &QTimer::timeout, this, &ClientManager::updateScreens);
}

ClientManager::~ClientManager() {
    qCDebug(lcClientManager) << "~ClientManager(): cleanupResources begin";
    if ( m_screenUpdateTimer ) {
        m_screenUpdateTimer->stop();
    }
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
    instance->instanceThread = new QThread(this);
    instance->instanceThread->setObjectName(QString("SessionThread-%1").arg(connectionId));
    qCDebug(lcClientManager) << "connectToHost(): created session thread for" << connectionId;

    // 创建 SessionManager（不设置父对象，以便移动到线程）
    instance->sessionManager = new SessionManager(connectionId, nullptr);

    // 将 SessionManager 移动到独立线程
    instance->sessionManager->moveToThread(instance->instanceThread);
    qCDebug(lcClientManager) << "connectToHost(): moved SessionManager to independent thread";

    // 创建远程桌面窗口（必须在主线程中，因为它是 QWidget）
    instance->remoteDesktopWindow = createRemoteDesktopWindow(instance->sessionManager);
    if ( !instance->remoteDesktopWindow ) {
        qCWarning(lcClientManager) << "connectToHost(): failed to create remote desktop window";
        // 清理线程和 SessionManager
        instance->instanceThread->quit();
        instance->instanceThread->wait();
        delete instance->sessionManager;
        delete instance->instanceThread;
        delete instance;
        return QString();
    } else {
        qCDebug(lcClientManager) << "connectToHost(): created remote desktop window for" << connectionId;
        instance->remoteDesktopWindow->updateWindowTitle(host);
    }

    // 注意：ClientRemoteWindow 继承自 QWidget（QGraphicsView），
    // QWidget 及其子类必须在主线程中创建和销毁，不能移动到其他线程。
    // 因此我们不将 remoteDesktopWindow 移动到 session thread，保持在主线程中运行。
    qCDebug(lcClientManager) << "connectToHost(): remoteDesktopWindow created and kept in main thread";

    // 启动线程
    instance->instanceThread->start();
    qCDebug(lcClientManager) << "connectToHost(): session thread started";

    // 注册到连接表
    m_connections.insert(instance->connectionId, instance);

    // 启动定时器（如果还未启动）
    if ( !m_screenUpdateTimer->isActive() ) {
        qCDebug(lcClientManager) << "connectToHost(): Starting screen update timer";
        m_screenUpdateTimer->start();
    }

    // 连接到 SessionManager 的连接状态变化信号，监听认证成功和连接建立事件
    connect(instance->sessionManager, &SessionManager::connectionStateChanged,
        this, [this, sessionManager = instance->sessionManager](ConnectionManager::ConnectionState state) {
            switch (state) {
                case ConnectionManager::ConnectionState::Connected:
                    // 连接建立成功
                    qCDebug(lcClientManager) << "Connection established for" << sessionManager->connectionId();
                    emit connectionEstablished(sessionManager->connectionId());
                    break;
                case ConnectionManager::ConnectionState::Authenticated:
                    // 认证成功
                    qCDebug(lcClientManager) << "Authentication successful for" << sessionManager->connectionId();
                    // 认证成功后启动会话 (跨线程调用，使用QueuedConnection)
                    QMetaObject::invokeMethod(sessionManager, "startSession", Qt::QueuedConnection);
                    break;
                default:
                    break;
            }
        }, Qt::QueuedConnection);
    qCDebug(lcClientManager) << "connectToHost(): connected to session state change signals";

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
    qCInfo(lcClientManager) << "disconnectFromHost(): [START] Disconnecting" << connectionId;
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    
    if ( !instance ) {
        qCWarning(lcClientManager) << "disconnectFromHost(): No instance found for" << connectionId;
        return;
    }
    
    if ( instance->isBeingDeleted ) {
        qCDebug(lcClientManager) << "disconnectFromHost(): Instance already being deleted for" << connectionId;
        return;
    }
    
    // 设置删除标志，防止重复删除
    instance->isBeingDeleted = true;
    
    // 先从连接列表中移除
    m_connections.remove(connectionId);
    
    if ( instance->sessionManager ) {
        qCDebug(lcClientManager) << "disconnectFromHost(): [STEP-1] Requesting session disconnect for" << connectionId;
        // 使用 Qt::QueuedConnection 进行跨线程调用
        QMetaObject::invokeMethod(instance->sessionManager,
            "disconnectFromHost",
            Qt::QueuedConnection);
    }
    
    // 关闭远程桌面窗口
    if ( instance->remoteDesktopWindow ) {
        if ( !instance->remoteDesktopWindow->isClosing() ) {
            qCDebug(lcClientManager) << "disconnectFromHost(): [STEP-2] Requesting window close for" << connectionId;
            instance->remoteDesktopWindow->close();
        } else {
            qCDebug(lcClientManager) << "disconnectFromHost(): [STEP-2] Window already closing for" << connectionId;
        }
    }
    
    // 清理连接
    qCDebug(lcClientManager) << "disconnectFromHost(): [STEP-3] Cleaning up connection for" << connectionId;
    cleanupConnection(instance);
    
    // 如果没有活动连接了，停止定时器
    if ( m_connections.isEmpty() && m_screenUpdateTimer->isActive() ) {
        qCDebug(lcClientManager) << "disconnectFromHost(): No more connections, stopping screen update timer";
        m_screenUpdateTimer->stop();
    }
    
    qCInfo(lcClientManager) << "disconnectFromHost(): [COMPLETE] Disconnected" << connectionId;
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
    m_screenUpdateTimer->start();
}

void ClientManager::onConnectionClosed() {
    SessionManager* sessionManager = qobject_cast<SessionManager*>(sender());
    if ( !sessionManager ) {
        qCDebug(lcClientManager) << "onConnectionClosed(): invalid sender";
        return;
    }
    
    QString connectionId = sessionManager->connectionId();
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    
    // 如果实例不存在或已被标记删除，说明已经在其他地方处理了
    if ( !instance ) {
        qCDebug(lcClientManager) << "onConnectionClosed(): Instance already removed for" << connectionId;
        return;
    }
    
    if ( instance->isBeingDeleted ) {
        qCDebug(lcClientManager) << "onConnectionClosed(): Instance already being deleted for" << connectionId;
        return;
    }
    
    qCDebug(lcClientManager) << "onConnectionClosed(): [START] Processing for" << connectionId;

    // 设置删除标志，防止重复删除
    instance->isBeingDeleted = true;

    // 先从连接列表中移除
    m_connections.remove(connectionId);

    // 关闭远程桌面窗口
    if ( instance->remoteDesktopWindow ) {
        if ( !instance->remoteDesktopWindow->isClosing() ) {
            qCDebug(lcClientManager) << "onConnectionClosed(): [STEP-1] Requesting window close for" << connectionId;
            instance->remoteDesktopWindow->close();
        } else {
            qCDebug(lcClientManager) << "onConnectionClosed(): [STEP-1] Window already closing for" << connectionId;
        }
    }
    
    // 清理连接
    qCDebug(lcClientManager) << "onConnectionClosed(): [STEP-2] Cleaning up connection for" << connectionId;
    cleanupConnection(instance);
    
    // 如果没有活动连接了，停止定时器
    if ( m_connections.isEmpty() && m_screenUpdateTimer->isActive() ) {
        qCDebug(lcClientManager) << "onConnectionClosed(): No more connections, stopping screen update timer";
        m_screenUpdateTimer->stop();
    }
    
    qCInfo(lcClientManager) << "onConnectionClosed(): [COMPLETE] Processed for" << connectionId;
}

void ClientManager::onConnectionError(const QString& error) {
    SessionManager* sessionManager = qobject_cast<SessionManager*>(sender());
    if ( !sessionManager ) {
        qCDebug(lcClientManager) << "onConnectionError(): invalid sender";
        return;
    }
    
    QString connectionId = sessionManager->connectionId();
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    
    // 如果实例不存在或已被标记删除，说明已经在其他地方处理了
    if ( !instance ) {
        qCDebug(lcClientManager) << "onConnectionError(): Instance already removed for" << connectionId;
        return;
    }
    
    if ( instance->isBeingDeleted ) {
        qCDebug(lcClientManager) << "onConnectionError(): Instance already being deleted for" << connectionId;
        return;
    }
    
    qCDebug(lcClientManager) << "onConnectionError(): [START] Processing error:" << error;

    // 设置删除标志，防止重复删除
    instance->isBeingDeleted = true;

    // 先从连接列表中移除
    m_connections.remove(connectionId);

    // 显示错误对话框
    QMessageBox msgBox(instance->remoteDesktopWindow);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(tr("服务器错误"));
    msgBox.setText(tr("连接服务器时发生错误：%1").arg(error));
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    
    // 清理连接
    qCDebug(lcClientManager) << "onConnectionError(): [STEP-1] Cleaning up connection for" << connectionId;
    cleanupConnection(instance);
    
    // 如果没有活动连接了，停止定时器
    if ( m_connections.isEmpty() && m_screenUpdateTimer->isActive() ) {
        qCDebug(lcClientManager) << "onConnectionError(): No more connections, stopping screen update timer";
        m_screenUpdateTimer->stop();
    }
    
    qCInfo(lcClientManager) << "onConnectionError(): [COMPLETE] Processed error for" << connectionId;
}

void ClientManager::onWindowClosed() {
    ClientRemoteWindow* window = qobject_cast<ClientRemoteWindow*>(sender());
    if ( !window ) {
        qCWarning(lcClientManager) << "onWindowClosed(): invalid sender";
        return;
    }

    QString connectionId = window->connectionId();
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    if ( instance && !instance->isBeingDeleted ) {
        qCInfo(lcClientManager) << "onWindowClosed(): [START] Processing window close for" << connectionId;

        // 设置删除标志，防止重复删除
        instance->isBeingDeleted = true;

        // 先从连接列表中移除，防止其他回调再次访问
        m_connections.remove(connectionId);

        // 异步断开连接并清理资源（使用 QueuedConnection 确保顺序执行）
        if ( instance->sessionManager ) {
            qCDebug(lcClientManager) << "onWindowClosed(): [STEP-1] Requesting session termination for" << connectionId;
            // 先终止会话（停止统计、清理本地数据）
            QMetaObject::invokeMethod(instance->sessionManager,
                "terminateSession",
                Qt::QueuedConnection);
            
            qCDebug(lcClientManager) << "onWindowClosed(): [STEP-2] Requesting disconnect for" << connectionId;
            // 再断开网络连接（发送断开请求、关闭套接字）
            QMetaObject::invokeMethod(instance->sessionManager,
                "disconnectFromHost",
                Qt::QueuedConnection);
        }

        qCDebug(lcClientManager) << "onWindowClosed(): [STEP-3] Cleaning up connection for" << connectionId;
        cleanupConnection(instance);

        // 检查是否所有连接都已关闭
        if ( m_connections.isEmpty() ) {
            // 停止定时器
            if ( m_screenUpdateTimer->isActive() ) {
                qCDebug(lcClientManager) << "onWindowClosed(): No more connections, stopping screen update timer";
                m_screenUpdateTimer->stop();
            }
            qCInfo(lcClientManager) << "onWindowClosed(): [COMPLETE] All connections closed, emitting signal";
            emit allConnectionsClosed();
        } else {
            qCDebug(lcClientManager) << "onWindowClosed(): [COMPLETE] Remaining connections:" << m_connections.size();
        }
    } else if ( instance && instance->isBeingDeleted ) {
        qCDebug(lcClientManager) << "onWindowClosed(): instance already being deleted for" << connectionId;
    } else {
        qCWarning(lcClientManager) << "onWindowClosed(): no instance found for window" << connectionId;
    }
}

void ClientManager::cleanupResources() {
    qCInfo(lcClientManager) << "cleanupResources(): [START] Cleaning up all connections, count:" << m_connections.size();

    // 【修复段错误】安全清理所有连接，防止重复删除
    try {
        // 创建连接实例的副本，避免在迭代过程中修改容器
        QList<ConnectionInstance*> instances;
        int markedCount = 0;
        for ( auto it = m_connections.begin(); it != m_connections.end(); ++it ) {
            if ( it.value() && !it.value()->isBeingDeleted ) {
                it.value()->isBeingDeleted = true;  // 设置删除标志
                instances.append(it.value());
                markedCount++;
            }
        }

        qCDebug(lcClientManager) << "cleanupResources(): Marked" << markedCount << "connections for deletion";

        // 清空连接映射
        m_connections.clear();

        // 安全删除所有连接实例
        for ( ConnectionInstance* instance : instances ) {
            qCDebug(lcClientManager) << "cleanupResources(): Cleaning up connection" << instance->connectionId;
            cleanupConnection(instance);
        }

        qCInfo(lcClientManager) << "cleanupResources(): [COMPLETE] Cleaned" << instances.size() << "connections";
    } catch ( const std::exception& e ) {
        qCCritical(lcClientManager) << "cleanupResources(): Exception during cleanup:" << e.what();
    } catch ( ... ) {
        qCCritical(lcClientManager) << "cleanupResources(): Unknown exception during cleanup";
    }
}

void ClientManager::cleanupConnection(ConnectionInstance* instance) {
    if ( !instance ) {
        qCWarning(lcClientManager) << "cleanupConnection(): Null instance provided";
        return;
    }

    qCDebug(lcClientManager) << "cleanupConnection(): [START] Cleanup for" << instance->connectionId;
    // ConnectionInstance的析构函数会自动处理资源清理（五阶段清理）
    delete instance;
    qCDebug(lcClientManager) << "cleanupConnection(): [COMPLETE] Instance deleted";
}

QString ClientManager::generateConnectionId() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

ConnectionInstance* ClientManager::getConnectionInstance(const QString& connectionId) const {
    return m_connections.value(connectionId, nullptr);
}

void ClientManager::updateScreens() {
    // 遍历所有连接，从各自的SessionManager中获取图片
    for ( auto it = m_connections.begin(); it != m_connections.end(); ++it ) {
        ConnectionInstance* instance = it.value();
        if ( !instance || !instance->sessionManager || !instance->remoteDesktopWindow ) {
            continue;
        }

        // 检查SessionManager是否有新图片
        if ( instance->sessionManager->hasScreenImage() ) {
            // 获取图片并更新窗口
            QImage image = instance->sessionManager->dequeueScreenImage();
            if ( !image.isNull() ) {
                instance->remoteDesktopWindow->updateRemoteScreen(image);
            }
        }
    }
}