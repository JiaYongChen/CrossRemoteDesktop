#include "ServerManager.h"
#include <atomic>
#include "ServerWorker.h"
#include "../common/core/threading/ThreadManager.h"
#include "../common/core/network/Protocol.h"
#include <QMutexLocker>
#include <QLoggingCategory>
#include <QTimer>
#include <memory>
// 新增：引入日志分类声明头，使用统一的日志分类（lcServerManager）
#include "../common/core/logging/LoggingCategories.h"

ServerManager::ServerManager(QObject *parent)
    : QObject(parent)
    , m_threadManager(ThreadManager::instance())
    , m_isServerRunning(false)
    , m_currentPort(0)
{
    // 设置与ServerWorker的信号连接
    setupWorkerConnections();
}

ServerManager::~ServerManager() {
    // 使用分类日志，便于按模块过滤与定位
    qCDebug(lcServerManager) << "ServerManager析构函数";
    gracefulShutdown();
    // 断开与ServerWorker的信号连接
    disconnectWorkerSignals();
}

bool ServerManager::startServer(quint16 port, const QString &password)
{
    // 1. 首先检查状态（避免长时间持有锁）
    {
        QMutexLocker stateLock(&m_stateMutex);
        if (m_isServerRunning) {
            qCDebug(lcServerManager) << "Server is already running";
            return false;
        }
    }
    
    // 2. 创建和启动线程（独立的锁作用域）
    bool threadCreated = false;
    {
        QMutexLocker workerLock(&m_workerMutex);
        
        if (!m_threadManager->createThread("ServerWorker", std::make_unique<ServerWorker>())) {
            qCDebug(lcServerManager) << "Failed to create ServerWorker thread";
            return false;
        }
        
        // 启动线程
        if (!m_threadManager->startThread("ServerWorker")) {
            qCDebug(lcServerManager) << "Failed to start ServerWorker thread";
            m_threadManager->destroyThread("ServerWorker");
            return false;
        }
        
        threadCreated = true;
    }
    
    if (!threadCreated) {
        return false;
    }
    
    // 3. 获取worker并启动服务器（不持有其他锁，避免死锁）
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        qWarning() << "Failed to get ServerWorker instance";
        // 清理线程
        {
            QMutexLocker workerLock(&m_workerMutex);
            m_threadManager->stopThread("ServerWorker");
            m_threadManager->destroyThread("ServerWorker");
        }
        return false;
    }
    
    // 4. 设置服务器参数并启动（避免死锁）
    // 先设置密码和端口，然后在Worker初始化完成后自动启动
    if (!password.isEmpty()) {
        QMetaObject::invokeMethod(worker, "setPassword", Qt::DirectConnection,
                                  Q_ARG(QString, password));
    }
    
    // 使用定时器延迟启动服务器，避免在Worker启动过程中调用
    QTimer::singleShot(100, [worker, port]() {
        QMetaObject::invokeMethod(worker, "startServer", Qt::QueuedConnection,
                                  Q_ARG(quint16, port));
    });
    
    // 5. 更新状态（最后更新，确保服务器启动成功）
    {
        QMutexLocker stateLock(&m_stateMutex);
        m_isServerRunning = true;
        m_currentPort = port;
    }
    qCDebug(lcServerManager) << "Server start initiated on port:" << port;
    return true;
}

void ServerManager::stopServer() {
    qCDebug(lcServerManager) << "停止服务器...";
    if (m_shuttingDown.exchange(true)) {
        qCDebug(lcServerManager) << "服务器已在关闭过程中";
        return;
    }
    
    // 不持有锁的情况下获取worker指针
    ServerWorker* worker = nullptr;
    {
        QMutexLocker locker(&m_workerMutex);
        worker = getServerWorker();
    }
    
    if (worker) {
        // 发送异步停止信号，避免阻塞
        QMetaObject::invokeMethod(worker, "stopServer", Qt::QueuedConnection, Q_ARG(bool, false));
        
        // 不等待，立即继续
    }
    
    // 在没有锁的情况下停止线程（不销毁，让程序退出时自动清理）
    if (m_threadManager) {
        qCDebug(lcServerManager) << "开始停止ServerWorker线程...";
        m_threadManager->stopThread("ServerWorker", false); // 异步停止，不等待完成
        qCDebug(lcServerManager) << "ServerWorker线程停止请求已发送";
    }
    qCDebug(lcServerManager) << "服务器已停止";
}

bool ServerManager::isServerRunning() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_isServerRunning;
}

quint16 ServerManager::getCurrentPort() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_currentPort;
}

bool ServerManager::isRunning() const
{
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        return false;
    }
    
    bool running = false;
    QMetaObject::invokeMethod(worker, "isRunning", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, running));
    return running;
}

quint16 ServerManager::getPort() const
{
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        return 0;
    }
    
    quint16 port = 0;
    QMetaObject::invokeMethod(worker, "getPort", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(quint16, port));
    return port;
}

int ServerManager::getConnectedClientCount() const
{
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        return 0;
    }
    
    int count = 0;
    QMetaObject::invokeMethod(worker, "getConnectedClientCount", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(int, count));
    return count;
}

QStringList ServerManager::getConnectedClients() const
{
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        return QStringList();
    }
    
    QStringList clients;
    QMetaObject::invokeMethod(worker, "getConnectedClients", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(QStringList, clients));
    return clients;
}

bool ServerManager::isClientConnected(const QString &clientAddress) const
{
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        return false;
    }
    
    bool connected = false;
    QMetaObject::invokeMethod(worker, "isClientConnected", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, connected),
                              Q_ARG(QString, clientAddress));
    return connected;
}

bool ServerManager::hasConnectedClients() const
{
    return getConnectedClientCount() > 0;
}

bool ServerManager::hasAuthenticatedClients() const
{
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        return false;
    }
    
    bool hasAuthenticated = false;
    QMetaObject::invokeMethod(worker, "hasAuthenticatedClients", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, hasAuthenticated));
    return hasAuthenticated;
}

// 信号槽处理方法 - 转发ServerWorker的信号
void ServerManager::onWorkerServerStarted(quint16 port)
{
    {
        QMutexLocker lock(&m_stateMutex);
        m_isServerRunning = true;
        m_currentPort = port;
    }
    qCDebug(lcServerManager) << "onWorkerServerStarted(): server started on port" << port;
    emit serverStarted(port);
}

void ServerManager::onWorkerServerStopped()
{
    {
        QMutexLocker lock(&m_stateMutex);
        m_isServerRunning = false;
        m_currentPort = 0;
    }
    qCDebug(lcServerManager) << "onWorkerServerStopped(): server stopped";
    emit serverStopped();
}

void ServerManager::onWorkerServerError(const QString &error)
{
    qCDebug(lcServerManager) << "onWorkerServerError():" << error;
    emit serverError(error);
}

void ServerManager::onWorkerClientConnected(const QString &clientAddress)
{
    qCDebug(lcServerManager) << "onWorkerClientConnected():" << clientAddress;
    emit clientConnected(clientAddress);
}

void ServerManager::onWorkerClientDisconnected(const QString &clientAddress)
{
    qCDebug(lcServerManager) << "onWorkerClientDisconnected():" << clientAddress;
    emit clientDisconnected(clientAddress);
}

void ServerManager::onWorkerClientAuthenticated(const QString &clientAddress)
{
    qCDebug(lcServerManager) << "onWorkerClientAuthenticated():" << clientAddress;
    emit clientAuthenticated(clientAddress);
}

void ServerManager::onWorkerMessageReceived(const QString &clientAddress, MessageType type, const QByteArray &message)
{
    // 处理从ServerWorker接收到的消息
    qCDebug(lcServerManager) << "onWorkerMessageReceived(): from" << clientAddress << "type:" << static_cast<int>(type);
    Q_UNUSED(message);
}

void ServerManager::setupWorkerConnections()
{
    // 注意：这里不能直接连接，因为ServerWorker还没有创建
    // 连接将在ServerWorker创建后通过ThreadManager建立
    // 监听ThreadManager的线程启动信号，当ServerWorker线程启动后建立信号连接
    connect(m_threadManager, &ThreadManager::threadStarted, this, 
            [this](const QString& threadName) {
                if (threadName == "ServerWorker") {
                    // 延迟一点时间确保Worker完全初始化
                    QTimer::singleShot(50, this, [this]() {
                        this->connectToServerWorker();
                    });
                }
            }, Qt::QueuedConnection);
}

void ServerManager::disconnectWorkerSignals()
{
    // 断开与ServerWorker的所有信号连接
    ServerWorker* worker = getServerWorker();
    if (worker) {
        disconnect(worker, nullptr, this, nullptr);
    }
}

void ServerManager::connectToServerWorker()
{
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        qCDebug(lcServerManager) << "ServerManager::connectToServerWorker() - Failed to get ServerWorker instance";
        return;
    }
    qCDebug(lcServerManager) << "ServerManager::connectToServerWorker() - Connecting signals to ServerWorker";
    
    // 连接服务器状态信号
    connect(worker, &ServerWorker::serverStarted, this, &ServerManager::onWorkerServerStarted, Qt::QueuedConnection);
    connect(worker, &ServerWorker::serverStopped, this, &ServerManager::onWorkerServerStopped, Qt::QueuedConnection);
    connect(worker, &ServerWorker::serverError, this, &ServerManager::onWorkerServerError, Qt::QueuedConnection);
    
    // 连接客户端状态信号
    connect(worker, &ServerWorker::clientConnected, this, &ServerManager::onWorkerClientConnected, Qt::QueuedConnection);
    connect(worker, &ServerWorker::clientDisconnected, this, &ServerManager::onWorkerClientDisconnected, Qt::QueuedConnection);
    connect(worker, &ServerWorker::clientAuthenticated, this, &ServerManager::onWorkerClientAuthenticated, Qt::QueuedConnection);
    
    // 连接消息接收信号
    connect(worker, &ServerWorker::messageReceived, this, &ServerManager::onWorkerMessageReceived, Qt::QueuedConnection);
    
    qCDebug(lcServerManager) << "ServerManager::connectToServerWorker() - All signals connected successfully";
}

ServerWorker* ServerManager::getServerWorker() const
{
    QMutexLocker lock(&m_workerMutex);
    
    const ThreadManager::ThreadInfo* threadInfo = m_threadManager->getThreadInfo("ServerWorker");
    if (!threadInfo || !threadInfo->worker) {
        return nullptr;
    }
    
    return qobject_cast<ServerWorker*>(threadInfo->worker);
}

// 客户端管理方法实现
int ServerManager::clientCount() const
{
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        return 0;
    }
    
    int count = 0;
    QMetaObject::invokeMethod(worker, "clientCount", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(int, count));
    return count;
}

QStringList ServerManager::connectedClients() const
{
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        return QStringList();
    }
    
    QStringList clients;
    QMetaObject::invokeMethod(worker, "connectedClients", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(QStringList, clients));
    return clients;
}

void ServerManager::setPassword(const QString &password)
{
    QMutexLocker lock(&m_stateMutex);
    m_password = password;
    
    ServerWorker* worker = getServerWorker();
    if (worker) {
        QMetaObject::invokeMethod(worker, "setPassword", Qt::QueuedConnection,
                                  Q_ARG(QString, password));
    }
}

QString ServerManager::password() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_password;
}

bool ServerManager::verifyPassword(const QString &password) const
{
    ServerWorker* worker = getServerWorker();
    if (!worker) {
        return false;
    }
    
    bool verified = false;
    QMetaObject::invokeMethod(worker, "verifyPassword", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, verified),
                              Q_ARG(QString, password));
    return verified;
}

void ServerManager::sendMessageToClient(const QString &clientAddress, MessageType type, const QByteArray &data)
{
    ServerWorker* worker = getServerWorker();
    if (worker) {
        QMetaObject::invokeMethod(worker, "sendMessageToClient", Qt::QueuedConnection,
                                  Q_ARG(QString, clientAddress),
                                  Q_ARG(MessageType, type),
                                  Q_ARG(QByteArray, data));
    }
}

void ServerManager::disconnectClient(const QString &clientAddress)
{
    ServerWorker* worker = getServerWorker();
    if (worker) {
        QMetaObject::invokeMethod(worker, "disconnectClient", Qt::QueuedConnection,
                                  Q_ARG(QString, clientAddress));
    }
}

void ServerManager::gracefulShutdown() {
    qDebug() << "开始优雅关闭ServerManager...";
    
    // 设置关闭标志
    m_shuttingDown = true;
    
    // 停止服务器（异步，不等待）
    stopServer();
    
    // 不等待，立即返回，让程序快速退出
    qDebug() << "ServerManager优雅关闭完成";
}
