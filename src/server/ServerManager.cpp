#include "ServerManager.h"
#include <atomic>
#include "service/ServerWorker.h"
#include "dataprocessing/DataProcessingWorker.h"
#include "dataprocessing/DataProcessingConfig.h"
#include "capture/ScreenCapture.h"
#include "clienthandler/ClientHandlerWorker.h"
#include "../common/core/threading/ThreadManager.h"
#include "../common/core/network/Protocol.h"
#include "dataflow/QueueManager.h"
#include "../common/core/config/Constants.h"
#include <QMutexLocker>
#include <QLoggingCategory>
#include <QTimer>
#include <memory>
// 新增：引入日志分类声明头，使用统一的日志分类（lcServerManager）
#include "../common/core/logging/LoggingCategories.h"

ServerManager::ServerManager(QObject* parent)
    : QObject(parent)
    , m_threadManager(ThreadManager::instance())
    , m_isServerRunning(false)
    , m_currentPort(0)
    , m_screenCapture(nullptr)
    , m_dataWorker(nullptr)
    , m_queueManager(nullptr)
    , m_currentClient(nullptr)
    , m_currentClientThreadName() {
    qCDebug(lcServerManager) << "初始化 ServerManager";

    // 创建队列管理器
    m_queueManager = QueueManager::instance();
    m_queueManager->initialize(120, 120); // 捕获队列120, 处理队列120

    // 创建屏幕捕获管理器（在主线程创建）
    m_screenCapture = new ScreenCapture(this);

    // 设置与ServerWorker的信号连接
    setupWorkerConnections();

    qCDebug(lcServerManager) << "ServerManager 初始化完成";
}

ServerManager::~ServerManager() {
    // 使用分类日志，便于按模块过滤与定位
    qCDebug(lcServerManager) << "ServerManager析构函数";

    // 停止屏幕捕获和数据处理
    stopWorkerThreads();

    // 清理客户端连接
    cleanupDisconnectedClient();

    // 只有在没有进行优雅关闭的情况下才调用gracefulShutdown
    if ( !m_gracefulShuttingDown.load() ) {
        qCDebug(lcServerManager) << "析构函数中执行优雅关闭";
        gracefulShutdown();
    } else {
        qCDebug(lcServerManager) << "已在优雅关闭过程中，跳过析构函数中的关闭操作";
    }

    // 断开与ServerWorker的信号连接
    disconnectWorkerSignals();

    // 清理屏幕捕获
    if ( m_screenCapture ) {
        m_screenCapture->deleteLater();
        m_screenCapture = nullptr;
    }

    m_dataWorker = nullptr; // 由ThreadManager管理生命周期
    m_queueManager = nullptr; // 单例，不需要删除

    qCDebug(lcServerManager) << "ServerManager 析构完成";
}

bool ServerManager::startServer(quint16 port, const QString& password) {
    // 1. 首先检查状态（避免长时间持有锁）
    {
        QMutexLocker stateLock(&m_stateMutex);
        if ( m_isServerRunning ) {
            qCDebug(lcServerManager) << "Server is already running";
            return false;
        }
    }

    // 2. 创建和启动线程（独立的锁作用域）
    bool threadCreated = false;
    {
        QMutexLocker workerLock(&m_workerMutex);

        if ( !m_threadManager->createThread("ServerWorker", std::make_unique<ServerWorker>()) ) {
            qCDebug(lcServerManager) << "Failed to create ServerWorker thread";
            return false;
        }

        // 启动线程
        if ( !m_threadManager->startThread("ServerWorker") ) {
            qCDebug(lcServerManager) << "Failed to start ServerWorker thread";
            m_threadManager->destroyThread("ServerWorker");
            return false;
        }

        threadCreated = true;
    }

    if ( !threadCreated ) {
        return false;
    }

    // 3. 获取worker并启动服务器（不持有其他锁，避免死锁）
    ServerWorker* worker = getServerWorker();
    if ( !worker ) {
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
    if ( !password.isEmpty() ) {
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
    if ( m_shuttingDown.exchange(true) ) {
        qCDebug(lcServerManager) << "服务器已在关闭过程中";
        return;
    }

    // 不持有锁的情况下获取worker指针
    ServerWorker* worker = nullptr;
    {
        QMutexLocker locker(&m_workerMutex);
        worker = getServerWorker();
    }

    if ( worker ) {
        // 发送异步停止信号，避免阻塞
        QMetaObject::invokeMethod(worker, "stopServer", Qt::QueuedConnection, Q_ARG(bool, false));
    }

    // 在没有锁的情况下停止线程（不销毁，让程序退出时自动清理）
    if ( m_threadManager ) {
        qCDebug(lcServerManager) << "开始停止ServerWorker线程...";
        m_threadManager->stopThread("ServerWorker", false); // 异步停止，不等待完成
        qCDebug(lcServerManager) << "ServerWorker线程停止请求已发送";
    }
    qCDebug(lcServerManager) << "服务器已停止";
}

bool ServerManager::isServerRunning() const {
    QMutexLocker lock(&m_stateMutex);
    return m_isServerRunning;
}

quint16 ServerManager::getCurrentPort() const {
    QMutexLocker lock(&m_stateMutex);
    return m_currentPort;
}

bool ServerManager::isRunning() const {
    ServerWorker* worker = getServerWorker();
    if ( !worker ) {
        return false;
    }

    bool running = false;
    QMetaObject::invokeMethod(worker, "isRunning", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(bool, running));
    return running;
}

quint16 ServerManager::getPort() const {
    ServerWorker* worker = getServerWorker();
    if ( !worker ) {
        return 0;
    }

    quint16 port = 0;
    QMetaObject::invokeMethod(worker, "getPort", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(quint16, port));
    return port;
}

int ServerManager::getConnectedClientCount() const {
    ServerWorker* worker = getServerWorker();
    if ( !worker ) {
        return 0;
    }

    int count = 0;
    QMetaObject::invokeMethod(worker, "getConnectedClientCount", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(int, count));
    return count;
}

QStringList ServerManager::getConnectedClients() const {
    ServerWorker* worker = getServerWorker();
    if ( !worker ) {
        return QStringList();
    }

    QStringList clients;
    QMetaObject::invokeMethod(worker, "getConnectedClients", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(QStringList, clients));
    return clients;
}

bool ServerManager::isClientConnected(const QString& clientAddress) const {
    ServerWorker* worker = getServerWorker();
    if ( !worker ) {
        return false;
    }

    bool connected = false;
    QMetaObject::invokeMethod(worker, "isClientConnected", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(bool, connected),
        Q_ARG(QString, clientAddress));
    return connected;
}

bool ServerManager::hasConnectedClients() const {
    return getConnectedClientCount() > 0;
}

bool ServerManager::hasAuthenticatedClients() const {
    ServerWorker* worker = getServerWorker();
    if ( !worker ) {
        return false;
    }

    bool hasAuthenticated = false;
    QMetaObject::invokeMethod(worker, "hasAuthenticatedClients", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(bool, hasAuthenticated));
    return hasAuthenticated;
}

// 信号槽处理方法 - 转发ServerWorker的信号
void ServerManager::onWorkerServerStarted(quint16 port) {
    {
        QMutexLocker lock(&m_stateMutex);
        m_isServerRunning = true;
        m_currentPort = port;
    }
    qCDebug(lcServerManager) << "onWorkerServerStarted(): server started on port" << port;
    emit serverStarted(port);
}

void ServerManager::onWorkerServerStopped() {
    {
        QMutexLocker lock(&m_stateMutex);
        m_isServerRunning = false;
        m_currentPort = 0;
        m_captureStarted = false;
    }
    qCDebug(lcServerManager) << "onWorkerServerStopped(): server stopped";
    // 同步输出一个非分类的信息日志，确保无论日志分类配置如何，最终态都能被捕获
    qInfo().noquote() << "服务器已停止";
    // 统一输出最终态日志，确保无论通过哪种关闭路径，都会有明确的“服务器已停止”信号
    // 使用信息级别日志以提高在不同环境下的可见性
    qCInfo(lcServerManager) << "服务器已停止";
    emit serverStopped();
}

void ServerManager::onWorkerServerError(const QString& error) {
    qCDebug(lcServerManager) << "onWorkerServerError():" << error;
    emit serverError(error);
}

void ServerManager::setupWorkerConnections() {
    // 注意：这里不能直接连接，因为ServerWorker还没有创建
    // 连接将在ServerWorker创建后通过ThreadManager建立
    // 监听ThreadManager的线程启动信号，当ServerWorker线程启动后建立信号连接
    connect(m_threadManager, &ThreadManager::threadStarted, this,
        [this](const QString& threadName) {
        if ( threadName == "ServerWorker" ) {
            // 延迟一点时间确保Worker完全初始化
            QTimer::singleShot(50, this, [this]() {
                this->connectToServerWorker();
            });
        }
    }, Qt::QueuedConnection);
}

void ServerManager::disconnectWorkerSignals() {
    // 断开与ServerWorker的所有信号连接
    ServerWorker* worker = getServerWorker();
    if ( worker ) {
        disconnect(worker, nullptr, this, nullptr);
    }
}

void ServerManager::connectToServerWorker() {
    ServerWorker* worker = getServerWorker();
    if ( !worker ) {
        qCDebug(lcServerManager) << "ServerManager::connectToServerWorker() - Failed to get ServerWorker instance";
        return;
    }
    qCDebug(lcServerManager) << "ServerManager::connectToServerWorker() - Connecting signals to ServerWorker";

    // 连接服务器状态信号
    connect(worker, &ServerWorker::serverStarted, this, &ServerManager::onWorkerServerStarted, Qt::QueuedConnection);
    connect(worker, &ServerWorker::serverStopped, this, &ServerManager::onWorkerServerStopped, Qt::QueuedConnection);
    connect(worker, &ServerWorker::serverError, this, &ServerManager::onWorkerServerError, Qt::QueuedConnection);

    // 连接新客户端连接信号（从ServerWorker转发TcpServer的信号）
    connect(worker, &ServerWorker::newClientConnection, this, &ServerManager::onNewClientConnection, Qt::QueuedConnection);

    qCDebug(lcServerManager) << "ServerManager::connectToServerWorker() - All signals connected successfully";
}

ServerWorker* ServerManager::getServerWorker() const {
    QMutexLocker lock(&m_workerMutex);

    const ThreadManager::ThreadInfo* threadInfo = m_threadManager->getThreadInfo("ServerWorker");
    if ( !threadInfo || !threadInfo->worker ) {
        return nullptr;
    }

    return qobject_cast<ServerWorker*>(threadInfo->worker);
}

DataProcessingWorker* ServerManager::getDataProcessingWorker() const {
    QMutexLocker lock(&m_workerMutex);

    const ThreadManager::ThreadInfo* threadInfo = m_threadManager->getThreadInfo("DataProcessingWorker");
    if ( !threadInfo || !threadInfo->worker ) {
        return nullptr;
    }

    return qobject_cast<DataProcessingWorker*>(threadInfo->worker);
}

// 客户端管理方法实现
int ServerManager::clientCount() const {
    ServerWorker* worker = getServerWorker();
    if ( !worker ) {
        return 0;
    }

    int count = 0;
    QMetaObject::invokeMethod(worker, "clientCount", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(int, count));
    return count;
}

QStringList ServerManager::connectedClients() const {
    ServerWorker* worker = getServerWorker();
    if ( !worker ) {
        return QStringList();
    }

    QStringList clients;
    QMetaObject::invokeMethod(worker, "connectedClients", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(QStringList, clients));
    return clients;
}

void ServerManager::sendMessageToClient(const QString& clientAddress, MessageType type, const QByteArray& data) {
    ServerWorker* worker = getServerWorker();
    if ( worker ) {
        QMetaObject::invokeMethod(worker, "sendMessageToClient", Qt::QueuedConnection,
            Q_ARG(QString, clientAddress),
            Q_ARG(MessageType, type),
            Q_ARG(QByteArray, data));
    }
}

void ServerManager::disconnectClient(const QString& clientAddress) {
    ServerWorker* worker = getServerWorker();
    if ( worker ) {
        QMetaObject::invokeMethod(worker, "disconnectClient", Qt::QueuedConnection,
            Q_ARG(QString, clientAddress));
    }
}

void ServerManager::gracefulShutdown() {
    qCDebug(lcServerManager) << "开始优雅关闭ServerManager...";

    bool wasAlreadyShuttingDown = m_gracefulShuttingDown.exchange(true);
    qCDebug(lcServerManager) << "gracefulShutdown标志检查 - 之前的值:" << wasAlreadyShuttingDown << "当前值:" << m_gracefulShuttingDown.load();

    if ( wasAlreadyShuttingDown ) {
        qCDebug(lcServerManager) << "ServerManager已在优雅关闭过程中，退出";
        return;
    }

    qCDebug(lcServerManager) << "继续执行优雅关闭逻辑...";

    // 不持有锁的情况下获取worker指针
    qCDebug(lcServerManager) << "准备获取ServerWorker指针...";
    ServerWorker* worker = nullptr;
    {
        qCDebug(lcServerManager) << "获取互斥锁...";
        QMutexLocker locker(&m_workerMutex);
        qCDebug(lcServerManager) << "直接访问ThreadManager，避免调用getServerWorker()造成死锁...";

        // 直接访问ThreadManager，避免调用getServerWorker()造成死锁
        const ThreadManager::ThreadInfo* threadInfo = m_threadManager->getThreadInfo("ServerWorker");
        if ( threadInfo && threadInfo->worker ) {
            worker = qobject_cast<ServerWorker*>(threadInfo->worker);
        }

        qCDebug(lcServerManager) << "ThreadManager访问完成，释放互斥锁...";
    }
    qCDebug(lcServerManager) << "互斥锁已释放，worker指针获取完成";

    qCDebug(lcServerManager) << "获取到的ServerWorker指针:" << worker;

    if ( worker ) {
        // 发送同步停止信号
        qCDebug(lcServerManager) << "停止ServerWorker...";
        bool invokeResult = QMetaObject::invokeMethod(worker, "stopServer", Qt::BlockingQueuedConnection, Q_ARG(bool, true));
        qCDebug(lcServerManager) << "ServerWorker停止调用结果:" << invokeResult;
        qCDebug(lcServerManager) << "ServerWorker已停止";
    } else {
        qCWarning(lcServerManager) << "无法获取ServerWorker实例，跳过停止操作";
    }

    // 同步停止ServerWorker线程（ServerWorker::cleanup 会统一停止并销毁相关工作线程）
    if ( m_threadManager ) {
        qCDebug(lcServerManager) << "停止ServerWorker线程...";
        m_threadManager->stopThread("ServerWorker", true); // 同步停止，等待完成
        qCDebug(lcServerManager) << "ServerWorker线程已停止";
    }

    // 到此为止：ServerWorker 与相关工作线程均已同步停止
    // 更新服务端运行状态
    {
        QMutexLocker lock(&m_stateMutex);
        m_isServerRunning = false;
        m_currentPort = 0;
    }
    // 统一输出最终态，供测试与运维脚本稳定识别（尽量提前打印，避免进程退出导致丢失）
    // 同步输出一个非分类的信息日志，确保无论日志分类配置如何，最终态都能被捕获
    qInfo().noquote() << "服务器已停止";
    qCInfo(lcServerManager) << "服务器已停止";
    // 同步输出debug级别日志，结合默认规则"*.debug=true"确保在所有配置下也能看到
    qCDebug(lcServerManager) << "服务器已停止";
    qCDebug(lcServerManager) << "ServerManager优雅关闭完成";
}

void ServerManager::stopWorkerThreads() {
    qCDebug(lcServerManager) << "停止工作线程（屏幕捕获与数据处理）";

    // 停止并销毁数据处理线程
    const QString dataWorkerName = QStringLiteral("DataProcessingWorker");
    if ( m_threadManager && m_threadManager->hasThread(dataWorkerName) ) {
        qCDebug(lcServerManager) << "停止 DataProcessingWorker 线程...";

        // 先请求工作器清空队列
        if ( m_dataWorker ) {
            QMetaObject::invokeMethod(m_dataWorker, "stopProcessingAndClearQueues", Qt::QueuedConnection);
        }

        // 同步停止线程
        bool stopSuccess = m_threadManager->stopThread(dataWorkerName, true);
        if ( stopSuccess ) {
            qCDebug(lcServerManager) << "DataProcessingWorker 线程已停止";
        } else {
            qCWarning(lcServerManager) << "停止 DataProcessingWorker 线程失败";
        }

        // 销毁线程
        bool destroySuccess = m_threadManager->destroyThread(dataWorkerName);
        if ( destroySuccess ) {
            qCDebug(lcServerManager) << "DataProcessingWorker 线程已销毁";
        } else {
            qCWarning(lcServerManager) << "销毁 DataProcessingWorker 线程失败";
        }

        m_dataWorker = nullptr;
    }

    // 停止屏幕捕获
    if ( m_screenCapture && m_screenCapture->isCapturing() ) {
        qCDebug(lcServerManager) << "停止屏幕捕获...";
        m_screenCapture->stopCapture();
        qCDebug(lcServerManager) << "屏幕捕获已停止";
    }

    // 更新状态标志
    {
        QMutexLocker lock(&m_stateMutex);
        m_captureStarted = false;
    }

    qCDebug(lcServerManager) << "工作线程停止完成";
}

void ServerManager::startWorkerThreads() {
    qCDebug(lcServerManager) << "启动工作线程（数据处理与屏幕捕获）";

    if ( !m_threadManager ) {
        qCWarning(lcServerManager) << "ThreadManager不存在，无法启动工作线程";
        return;
    }

    // 幂等保护：如果已启动则直接返回
    {
        QMutexLocker lock(&m_stateMutex);
        if ( m_captureStarted ) {
            qCDebug(lcServerManager) << "检测到工作线程已启动，跳过重复启动";
            return;
        }
    }

    // 1. 启动屏幕捕获
    if ( m_screenCapture ) {
        qCDebug(lcServerManager) << "启动屏幕捕获";
        m_screenCapture->startCapture();
        qCDebug(lcServerManager) << "屏幕捕获已启动";
    }

    // 2. 创建和启动 DataProcessingWorker 线程
    const QString dataWorkerName = QStringLiteral("DataProcessingWorker");
    if ( !m_threadManager->hasThread(dataWorkerName) ) {
        qCDebug(lcServerManager) << "创建 DataProcessingWorker 线程";

        // 创建数据处理配置
        auto processingConfig = std::make_shared<DataProcessingConfig>();

        // 创建数据处理工作线程
        auto dataWorker = std::make_unique<DataProcessingWorker>();
        DataProcessingWorker* dataWorkerPtr = dataWorker.get();
        dataWorkerPtr->setProcessingConfig(processingConfig);
        dataWorkerPtr->setMaxQueueSize(CoreConstants::Performance::MAX_QUEUE_SIZE);
        dataWorkerPtr->setProcessingTimeout(2000);

        if ( !m_threadManager->createThread(dataWorkerName, std::move(dataWorker), false, true, 3) ) {
            qCCritical(lcServerManager) << "创建 DataProcessingWorker 线程失败";
            m_dataWorker = nullptr;
            return;
        }

        if ( !m_threadManager->startThread(dataWorkerName) ) {
            qCCritical(lcServerManager) << "启动 DataProcessingWorker 线程失败";
            m_threadManager->destroyThread(dataWorkerName);
            m_dataWorker = nullptr;
            return;
        }

        m_dataWorker = dataWorkerPtr;
        qCDebug(lcServerManager) << "DataProcessingWorker 线程已创建并启动";

        // 恢复数据处理
        QMetaObject::invokeMethod(m_dataWorker, "resumeProcessing", Qt::QueuedConnection);
    } else {
        // 线程已存在，确保其处于运行状态
        const ThreadManager::ThreadInfo* info = m_threadManager->getThreadInfo(dataWorkerName);
        if ( info && info->worker ) {
            m_dataWorker = qobject_cast<DataProcessingWorker*>(info->worker);
        }

        if ( !m_threadManager->isThreadRunning(dataWorkerName) ) {
            qCDebug(lcServerManager) << "启动已存在的 DataProcessingWorker 线程";
            if ( m_threadManager->startThread(dataWorkerName) ) {
                qCDebug(lcServerManager) << "DataProcessingWorker 线程已启动";
            } else {
                qCWarning(lcServerManager) << "启动已存在的 DataProcessingWorker 线程失败";
            }
        }

        if ( m_dataWorker ) {
            qCDebug(lcServerManager) << "恢复 DataProcessingWorker 数据处理";
            QMetaObject::invokeMethod(m_dataWorker, "resumeProcessing", Qt::QueuedConnection);
        }
    }

    // 设置状态为已启动
    {
        QMutexLocker lock(&m_stateMutex);
        m_captureStarted = true;
    }

    qCDebug(lcServerManager) << "工作线程启动完成";
}

void ServerManager::onNewClientConnection(qintptr socketDescriptor) {
    qCDebug(lcServerManager) << "新客户端连接:" << socketDescriptor;

    QMutexLocker locker(&m_clientMutex);

    // 单连接模式：如果已有客户端连接，拒绝新连接
    if ( m_currentClient ) {
        qCDebug(lcServerManager) << "已有客户端连接，拒绝新连接";
        // 发送拒绝消息的逻辑可以在这里实现
        return;
    }

    // 创建ClientHandlerWorker实例
    auto worker = std::make_unique<ClientHandlerWorker>(socketDescriptor);

    // 保存Worker裸指针（在move之前）
    m_currentClient = worker.get();

    // 创建线程名称
    m_currentClientThreadName = QString("ClientHandler_%1").arg(socketDescriptor);

    // 使用ThreadManager创建并启动Worker线程
    if ( !m_threadManager->createThread(m_currentClientThreadName, std::move(worker), true) ) {
        qCCritical(lcServerManager) << "创建 ClientHandlerWorker 线程失败";
        m_currentClient = nullptr;
        m_currentClientThreadName.clear();
        return;
    }

    // Worker已经在新线程中，现在建立信号连接
    connect(m_currentClient, &ClientHandlerWorker::disconnected,
        this, &ServerManager::onClientHandlerDisconnected, Qt::QueuedConnection);

    connect(m_currentClient, &ClientHandlerWorker::authenticated,
        this, &ServerManager::onClientHandlerAuthenticated, Qt::QueuedConnection);

    connect(m_currentClient, &ClientHandlerWorker::errorOccurred,
        this, &ServerManager::onClientHandlerError, Qt::QueuedConnection);

    connect(m_currentClient, &ClientHandlerWorker::messageReceived,
        this, &ServerManager::onClientHandlerMessageReceived, Qt::QueuedConnection);

    qCDebug(lcServerManager) << "ClientHandlerWorker已启动在线程中";
}

void ServerManager::onClientHandlerDisconnected() {
    QString clientAddress;
    if ( m_currentClient ) {
        clientAddress = m_currentClient->clientAddress();
    }

    qCDebug(lcServerManager) << "客户端已断开:" << clientAddress;

    // 清理客户端
    cleanupDisconnectedClient();

    // 停止工作线程
    stopWorkerThreads();

    emit clientDisconnected(clientAddress);
}

void ServerManager::onClientHandlerAuthenticated() {
    if ( !m_currentClient ) return;

    QString clientAddress = m_currentClient->clientAddress();
    qCDebug(lcServerManager) << "客户端已认证:" << clientAddress;

    // 检查是否已启动工作线程
    bool alreadyStarted = false;
    {
        QMutexLocker lock(&m_stateMutex);
        alreadyStarted = m_captureStarted;
    }

    if ( !alreadyStarted ) {
        qCDebug(lcServerManager) << "首个客户端认证成功，启动DataProcessingWorker与ScreenCapture";
        startWorkerThreads();
    } else {
        qCDebug(lcServerManager) << "工作线程已处于运行状态，跳过重复启动";
    }

    emit clientAuthenticated(clientAddress);
}

void ServerManager::onClientHandlerError(const QString& error) {
    qCCritical(lcServerManager) << "客户端错误:" << error;
    emit serverError(error);
}

void ServerManager::onClientHandlerMessageReceived(MessageType type, const QByteArray& data) {
    qCDebug(lcServerManager) << "收到客户端消息，类型:" << static_cast<int>(type);

    // 这里可以处理客户端消息，例如输入事件等
    // 暂时不需要转发给 ServerWorker
    Q_UNUSED(type);
    Q_UNUSED(data);
}

void ServerManager::cleanupDisconnectedClient() {
    QMutexLocker locker(&m_clientMutex);

    if ( m_currentClient ) {
        // 断开Worker信号连接
        m_currentClient->disconnect(this);

        // 停止并销毁ClientHandlerWorker线程
        if ( !m_currentClientThreadName.isEmpty() && m_threadManager ) {
            m_threadManager->stopThread(m_currentClientThreadName, false);
            m_threadManager->destroyThread(m_currentClientThreadName);
        }

        m_currentClient = nullptr;
        m_currentClientThreadName.clear();
    }
}
