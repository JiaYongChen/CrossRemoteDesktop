#include "ClientManager.h"
#include "./network/ConnectionManager.h"
#include "./session/ProtocolSession.h"
#include "./session/DecodePipeline.h"
#include "./window/ClientRemoteWindow.h"
#ifndef QT_NO_OPENGL
#include "./window/GLTextureViewport.h"
#include "./decode/GpuDecodeTarget.h"
#endif
#include "../common/core/config/UiConstants.h"
#include "../common/core/logging/LoggingCategories.h"
#include "../common/core/threading/ThreadManager.h"
#include "./network/TcpClient.h"  // 新增：获取实际服务器IP地址

#include <chrono>

#include <QtCore/QSettings>
#include <QtCore/QDateTime>
#include <QtCore/QUuid>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QApplication>

// ConnectionInstance 方法实现

ConnectionInstance::~ConnectionInstance() {
    shutdown();
}

void ConnectionInstance::shutdown() {
    // Idempotent guard: prevent double-cleanup (uses dedicated flag, NOT isBeingDeleted).
    // isBeingDeleted is set by ClientManager callers (onWindowClosed, disconnectFromHost, etc.)
    // as a re-entry guard BEFORE calling cleanupConnection() → delete → ~ConnectionInstance().
    // If we checked isBeingDeleted here, shutdown would skip all 5 phases and never disconnect.
    if ( m_shutdownDone ) {
        return;
    }
    m_shutdownDone = true;
    isBeingDeleted = true;

    qCInfo(lcClientManager) << "ConnectionInstance::shutdown(): [START] Cleanup for connection:" << connectionId;

    try {
        // Phase 1: Close window and disconnect session
        shutdownPhase1_CloseWindowAndDisconnect();

        // Phase 2: Stop the worker thread gracefully
        shutdownPhase2_StopThread();

        // Phase 3: Delete ProtocolSession (thread already stopped)
        shutdownPhase3_DeleteProtocolComponents();

        // Phase 4: Schedule window deletion on main thread
        shutdownPhase4_DeleteWindow();

        // Phase 5: Delete thread object
        shutdownPhase5_DeleteThread();

        qCInfo(lcClientManager) << "ConnectionInstance::shutdown(): [COMPLETE] Cleanup completed successfully for" << connectionId;
    } catch ( const std::exception& e ) {
        qCWarning(lcClientManager) << "ConnectionInstance::shutdown(): exception during cleanup:" << e.what();
    } catch ( ... ) {
        qCWarning(lcClientManager) << "ConnectionInstance::shutdown(): unknown exception during cleanup";
    }
}

void ConnectionInstance::shutdownPhase1_CloseWindowAndDisconnect() {
    if ( remoteDesktopWindow && !remoteDesktopWindow.isNull() ) {
        if ( !remoteDesktopWindow->isClosing() ) {
            qCDebug(lcClientManager) << "shutdown [PHASE-1] Closing remote window for" << connectionId;
            remoteDesktopWindow->close();
        }
        remoteDesktopWindow->disconnect();
    }

    // 0. 先停止解码管线（消费者先停，再停生产者网络线程）
    if ( decodePipeline && !decodePipeline.isNull() && instanceThread && instanceThread->isRunning() ) {
        qCDebug(lcClientManager) << "shutdown [PHASE-1] Stopping decode pipeline for" << connectionId;
        QMetaObject::invokeMethod(decodePipeline.data(), [p = decodePipeline.data()]() {
            p->stop();
        }, Qt::BlockingQueuedConnection);
    }

    if ( protocolSession && !protocolSession.isNull() ) {
        qCDebug(lcClientManager) << "shutdown [PHASE-1] Disconnecting session for" << connectionId;
        Qt::ConnectionType connType = (instanceThread && instanceThread->isRunning())
            ? Qt::BlockingQueuedConnection
            : Qt::DirectConnection;
        bool invoked = QMetaObject::invokeMethod(protocolSession, "disconnectFromHost", connType);
        if ( !invoked ) {
            qCWarning(lcClientManager) << "shutdown [PHASE-1] Failed to invoke disconnectFromHost for" << connectionId;
        }

        // Stop all child timers before moveToThread to prevent cross-thread timer delivery
        if ( instanceThread && instanceThread->isRunning() ) {
            QMetaObject::invokeMethod(protocolSession.data(), [ps = protocolSession.data()]() {
                const auto timers = ps->findChildren<QTimer*>();
                for ( QTimer* timer : timers ) {
                    if ( timer->objectName() == "disconnectTimeoutTimer" ) {
                        continue;
                    }
                    timer->stop();
                }
            }, Qt::BlockingQueuedConnection);
        }

        // 将 ProtocolSession 移回主线程
        if ( instanceThread && instanceThread->isRunning() ) {
            QThread* mainThread = QThread::currentThread();
            QMetaObject::invokeMethod(protocolSession.data(), [ps = protocolSession.data(), mainThread]() {
                ps->moveToThread(mainThread);
            }, Qt::BlockingQueuedConnection);
        }
    }
}

void ConnectionInstance::shutdownPhase2_StopThread() {
    if ( !instanceThread || !instanceThread->isRunning() ) {
        return;
    }

    qCDebug(lcClientManager) << "shutdown [PHASE-2] Stopping session thread for" << connectionId;
    instanceThread->quit();

    if ( !instanceThread->wait(THREAD_QUIT_TIMEOUT_MS) ) {
        qCWarning(lcClientManager) << "shutdown [PHASE-2] Thread quit timeout after"
            << THREAD_QUIT_TIMEOUT_MS << "ms, requesting interruption for" << connectionId;
        instanceThread->requestInterruption();
        instanceThread->quit();
        if ( !instanceThread->wait(THREAD_TERMINATE_TIMEOUT_MS) ) {
            // Last resort: leak the thread rather than force-terminate (UB).
            qCCritical(lcClientManager) << "shutdown [PHASE-2] Thread still running after"
                << THREAD_TERMINATE_TIMEOUT_MS << "ms, leaking thread to avoid undefined behavior for" << connectionId;
            QObject::connect(instanceThread, &QThread::finished, instanceThread, &QObject::deleteLater);
            // Issue 1 Fix: 将父对象设为 nullptr，防止 ClientManager 析构时
            // QObject::~QObject() 删除仍在运行的线程，导致 qFatal/abort。
            instanceThread->setParent(nullptr);
            instanceThread = nullptr;
        }
    } else {
        qCDebug(lcClientManager) << "shutdown [PHASE-2] Thread stopped gracefully for" << connectionId;
    }
}

void ConnectionInstance::shutdownPhase3_DeleteProtocolComponents() {
    if ( !protocolSession || protocolSession.isNull() ) {
        return;
    }

    qCDebug(lcClientManager) << "shutdown [PHASE-3] Deleting protocol components for" << connectionId;

    // Deep-disconnect 整个对象树，防止删除时信号级联崩溃。
    // ProtocolSession 是父对象，删除时会自动级联删除 ConnectionManager、
    // DecodePipeline 及其子对象（TcpClient 等）。
    protocolSession->disconnect();
    const auto children = protocolSession->findChildren<QObject*>(Qt::FindDirectChildrenOnly);
    for ( QObject* child : children ) {
        child->disconnect();
        const auto grandchildren = child->findChildren<QObject*>();
        for ( QObject* gc : grandchildren ) {
            gc->disconnect();
        }
    }

    delete protocolSession.data();
    qCDebug(lcClientManager) << "shutdown [PHASE-3] Protocol components deleted for" << connectionId;
}

void ConnectionInstance::shutdownPhase4_DeleteWindow() {
    if ( remoteDesktopWindow && !remoteDesktopWindow.isNull() ) {
        qCDebug(lcClientManager) << "shutdown [PHASE-4] Scheduling window deletion for" << connectionId;
        remoteDesktopWindow->deleteLater();
    }
}

void ConnectionInstance::shutdownPhase5_DeleteThread() {
    if ( !instanceThread ) {
        return;
    }

    if ( instanceThread->isRunning() ) {
        qCWarning(lcClientManager) << "shutdown [PHASE-5] Thread still running, waiting again for" << connectionId;
        instanceThread->quit();
        instanceThread->wait(THREAD_QUIT_TIMEOUT_MS);
    }

    qCDebug(lcClientManager) << "shutdown [PHASE-5] Deleting thread object for" << connectionId;
    delete instanceThread;
    instanceThread = nullptr;
}

bool ConnectionInstance::isValid() const {
    return !connectionId.isEmpty() && !protocolSession.isNull();
}

QString ConnectionInstance::getConnectionState() const {
    if ( protocolSession.isNull() ) {
        return "Invalid";
    }

    // 使用 ProtocolSession 的方法
    if ( protocolSession->isAuthenticated() ) {
        return "Authenticated";
    } else if ( protocolSession->isConnected() ) {
        return "Connected";
    } else {
        return "Disconnected";
    }
}

QString ConnectionInstance::getHost() const {
    return protocolSession ? protocolSession->currentHost() : QString();
}

int ConnectionInstance::getPort() const {
    return protocolSession ? protocolSession->currentPort() : 0;
}

bool ConnectionInstance::isConnected() const {
    return protocolSession && protocolSession->isConnected();
}

bool ConnectionInstance::isAuthenticated() const {
    return protocolSession && protocolSession->isAuthenticated();
}

// ClientManager 方法实现

ClientManager::ClientManager(QObject* parent)
    : QObject(parent) {
    // Frame delivery now uses TripleBuffer (lock-free, atomic SPSC).
    // No timer or signal-based pump needed — GLTextureViewport::paintGL()
    // reads directly from the triple buffer.
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

    // 创建网络线程
    instance->instanceThread = new QThread(this);
    instance->instanceThread->setObjectName(QString("Network-%1").arg(connectionId));
    qCDebug(lcClientManager) << "connectToHost(): created network thread for" << connectionId;

    // 创建网络组件（ProtocolSession 作为父对象，统一级联删除）
    auto* connMgr = new ConnectionManager();
    instance->decodePipeline = new DecodePipeline(connectionId);
    instance->protocolSession = new ProtocolSession(connMgr, instance->decodePipeline);
    instance->protocolSession->setConnectionId(connectionId);

    // 设置父子关系：ProtocolSession 删除时自动级联删除 ConnectionManager / DecodePipeline
    connMgr->setParent(instance->protocolSession);
    instance->decodePipeline->setParent(instance->protocolSession);

    // 将网络组件移动到独立线程
    connMgr->moveToThread(instance->instanceThread);
    instance->decodePipeline->moveToThread(instance->instanceThread);
    instance->protocolSession->moveToThread(instance->instanceThread);
    qCDebug(lcClientManager) << "connectToHost(): moved network components to thread";

    // 创建远程桌面窗口（必须在主线程中，因为它是 QWidget）
    instance->remoteDesktopWindow = createRemoteDesktopWindow(instance->protocolSession);
    if ( !instance->remoteDesktopWindow ) {
        qCWarning(lcClientManager) << "connectToHost(): failed to create remote desktop window";
        // 清理线程和组件（protocolSession 作为父对象级联删除 connMgr 和 decodePipeline）
        instance->instanceThread->quit();
        instance->instanceThread->wait();
        delete instance->protocolSession;  // 级联删除 connMgr + decodePipeline
        delete instance->instanceThread;
        delete instance;
        return QString();
    } else {
        qCDebug(lcClientManager) << "connectToHost(): created remote desktop window for" << connectionId;
        instance->remoteDesktopWindow->updateWindowTitle(host);
    }

    // 注意：ClientRemoteWindow 继承自 QWidget，GLTextureViewport 为其子控件。
    // QWidget 及其子类必须在主线程中创建和销毁，不能移动到其他线程。
    // 因此我们不将 remoteDesktopWindow 移动到网络线程，保持在主线程中运行。
    qCDebug(lcClientManager) << "connectToHost(): remoteDesktopWindow created and kept in main thread";

    // 启动线程
    instance->instanceThread->start();
    qCDebug(lcClientManager) << "connectToHost(): network thread started";

    // 注册到连接表
    m_connections.insert(instance->connectionId, instance);

    // Triple-buffered lock-free frame delivery: attach decode pipeline's triple buffer
    // to the GL viewport so paintGL() reads frames directly via atomics.
#ifndef QT_NO_OPENGL
    {
        auto* gl = instance->remoteDesktopWindow->glViewport();
        if ( gl && instance->decodePipeline ) {
            gl->attachFrameBuffer(instance->decodePipeline->frameBuffer());

            // Wire worker-thread GL upload: when the GL context is ready,
            // store it so the DecodeWorker can initialize with a shared GL context
            // for direct GPU texture upload.
            instance->decodePipeline->setGLViewport(gl);

            QObject::connect(gl, &GLTextureViewport::glContextReady,
                instance->protocolSession,
                [pipeline = instance->decodePipeline.data(), gl](QOpenGLContext* ctx) {
                    pipeline->setGLContext(ctx);
                    GpuDecodeTarget* target = gl->decodeTarget();
                    if (target) {
                        pipeline->setDecodeTarget(target);
                    }
                }, Qt::QueuedConnection);

            // Guard against signal race: if initializeGL() already fired
            // (e.g., during processEvents in createRemoteDesktopWindow),
            // the signal was lost — set directly.
            if (gl->context() && gl->context()->isValid()) {
                instance->decodePipeline->setGLContext(gl->context());
                GpuDecodeTarget* target = gl->decodeTarget();
                if (target) {
                    instance->decodePipeline->setDecodeTarget(target);
                }
            }
        }
    }
#endif

    // 连接到 ProtocolSession 的连接状态变化信号，监听认证成功和连接建立事件
    connect(instance->protocolSession, &ProtocolSession::connectionStateChanged,
        this, [this, proto = instance->protocolSession](ConnectionManager::ConnectionState state) {
            switch (state) {
                case ConnectionManager::ConnectionState::Connected:
                    // 连接建立成功
                    qCDebug(lcClientManager) << "Connection established for" << proto->connectionId();
                    emit connectionEstablished(proto->connectionId());
                    break;
                case ConnectionManager::ConnectionState::Authenticated:
                    // 认证成功
                    qCDebug(lcClientManager) << "Authentication successful for" << proto->connectionId();
                    // 认证成功后启动会话 (跨线程调用，使用QueuedConnection)
                    QMetaObject::invokeMethod(proto, "startSession", Qt::QueuedConnection);
                    break;
                default:
                    break;
            }
        }, Qt::QueuedConnection);
    qCDebug(lcClientManager) << "connectToHost(): connected to session state change signals";

    // 使用 Qt::QueuedConnection 从主线程调用 ProtocolSession 的方法（跨线程调用）
    QMetaObject::invokeMethod(instance->protocolSession,
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
    
    if ( instance->protocolSession ) {
        qCDebug(lcClientManager) << "disconnectFromHost(): [STEP-1] Requesting session disconnect for" << connectionId;
        // 使用 Qt::QueuedConnection 进行跨线程调用
        QMetaObject::invokeMethod(instance->protocolSession,
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
        if ( it.value()->protocolSession && it.value()->protocolSession->isConnected() ) {
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

ProtocolSession* ClientManager::protocolSession(const QString& connectionId) const {
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->protocolSession : nullptr;
}

ClientRemoteWindow* ClientManager::remoteDesktopWindow(const QString& connectionId) const {
    ConnectionInstance* instance = getConnectionInstance(connectionId);
    return instance ? instance->remoteDesktopWindow : nullptr;
}

ClientRemoteWindow* ClientManager::createRemoteDesktopWindow(ProtocolSession* protocolSession) {
    if ( !protocolSession ) {
        qCDebug(lcClientManager) << "createRemoteDesktopWindow(): invalid protocolSession";
        return nullptr;
    }

    // 直接传入 ProtocolSession，构造函数会自动设置
    ClientRemoteWindow* remoteDesktopWindow = new ClientRemoteWindow(protocolSession, nullptr);

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
    ProtocolSession* sessionManager = qobject_cast<ProtocolSession*>(sender());
    if ( !sessionManager ) {
        qCDebug(lcClientManager) << "onConnectionEstablished(): invalid sender";
        return;
    }

    qCDebug(lcClientManager) << "onConnectionEstablished(): for" << sessionManager->connectionId();
    emit connectionEstablished(sessionManager->connectionId());
}

void ClientManager::onAuthenticated() {
    ProtocolSession* sessionManager = qobject_cast<ProtocolSession*>(sender());
    if ( !sessionManager ) {
        qCDebug(lcClientManager) << "onAuthenticated(): invalid sender";
        return;
    }

    qCDebug(lcClientManager) << "onAuthenticated(): start session for" << sessionManager->connectionId();
    // 认证成功后启动会话
    sessionManager->startSession();
}

void ClientManager::onConnectionClosed() {
    ProtocolSession* sessionManager = qobject_cast<ProtocolSession*>(sender());
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

    qCInfo(lcClientManager) << "onConnectionClosed(): [COMPLETE] Processed for" << connectionId;
}

void ClientManager::onConnectionError(const QString& error) {
    ProtocolSession* sessionManager = qobject_cast<ProtocolSession*>(sender());
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

        // 直接清理连接实例，~ConnectionInstance() 内部会通过 BlockingQueuedConnection
        // 在 session 线程中同步执行 disconnectFromHost，然后将对象移回主线程后安全删除
        qCDebug(lcClientManager) << "onWindowClosed(): [STEP-1] Cleaning up connection for" << connectionId;
        cleanupConnection(instance);

        // 检查是否所有连接都已关闭
        if ( m_connections.isEmpty() ) {
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