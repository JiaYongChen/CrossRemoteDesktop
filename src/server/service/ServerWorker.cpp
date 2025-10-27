#include "ServerWorker.h"
#include "TcpServer.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QMutexLocker>

ServerWorker::ServerWorker(QObject* parent)
    : Worker(parent)
    , m_tcpServer(nullptr)
    , m_stopTimeoutTimer(nullptr)
    , m_serverMutex()
    , m_isServerRunning(false)
    , m_currentPort(0) {
    setName("ServerWorker");
    qCDebug(lcServer, "初始化服务器工作线程（简化版-仅TcpServer）");
}

ServerWorker::~ServerWorker() {
    qCDebug(lcServer, "销毁服务器工作线程");

    // 确保服务器已停止
    if ( m_isServerRunning ) {
        stopServer(true);
    }
}

bool ServerWorker::initialize() {
    qCDebug(lcServer, "初始化服务器工作线程组件（简化版-仅TcpServer）");

    // 创建TCP服务器（必须在工作线程中创建）
    m_tcpServer = new TcpServer(this);

    // 创建停止超时定时器
    m_stopTimeoutTimer = new QTimer(this);
    m_stopTimeoutTimer->setSingleShot(true);
    m_stopTimeoutTimer->setInterval(5000); // 5秒超时
    connect(m_stopTimeoutTimer, &QTimer::timeout, this, &ServerWorker::onStopTimeout);

    // 设置服务器连接
    setupServerConnections();

    qCDebug(lcServer, "服务器工作线程初始化完成");
    return true;
}

void ServerWorker::cleanup() {
    qCDebug(lcServer, "清理服务器工作线程资源（简化版）");

    // 停止定时器
    if ( m_stopTimeoutTimer ) {
        m_stopTimeoutTimer->stop();
    }

    // 断开服务器信号连接
    disconnectServerSignals();

    // 停止TCP服务器
    if ( m_tcpServer ) {
        m_tcpServer->stopServer(true);
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }

    qCDebug(lcServer, "服务器工作线程资源清理完成");
}

void ServerWorker::processTask() {
    // 简化版本：仅处理Qt事件循环
    // TcpServer 相关事件通过信号槽机制处理
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    QThread::msleep(1);
}

bool ServerWorker::startServer(quint16 port) {
    QMutexLocker locker(&m_serverMutex);

    if ( m_isServerRunning ) {
        qCDebug(lcServer, "服务器已在运行中");
        return true;
    }

    qCDebug(lcServer) << "启动服务器，端口:" << port;

    // 确保工作线程已启动
    if ( !isRunning() ) {
        start();
        // 等待线程启动完成
        QThread::msleep(100);
    }

    // 直接在当前线程中执行，避免死锁
    bool result = false;
    if ( !m_tcpServer ) {
        qCDebug(lcServer, "TCP服务器未初始化");
        return false;
    }

    result = m_tcpServer->startServer(port);
    if ( result ) {
        m_currentPort = m_tcpServer->serverPort();
        m_isServerRunning = true;

        emit serverStarted(m_currentPort);

        qCDebug(lcServer, "服务器启动成功，端口: %d", m_currentPort);
    } else {
        emit serverError(tr("服务器启动失败"));
        qCDebug(lcServer, "服务器启动失败");
    }

    return result;
}

void ServerWorker::stopServer(bool synchronous) {
    QMutexLocker locker(&m_serverMutex);

    if ( !m_isServerRunning ) {
        qCDebug(lcServer, "服务器未运行，无需停止");
        return;
    }

    qCDebug(lcServer, "停止服务器，同步模式: %s", synchronous ? "true" : "false");

    // 简化版本：仅停止TcpServer
    if ( m_tcpServer ) {
        m_tcpServer->stopServer(synchronous);
    }

    m_isServerRunning = false;
    m_currentPort = 0;

    emit serverStopped();
    qCDebug(lcServer, "服务器停止完成");
}

bool ServerWorker::isServerRunning() const {
    QMutexLocker locker(&m_serverMutex);
    return m_isServerRunning;
}

quint16 ServerWorker::getCurrentPort() const {
    QMutexLocker locker(&m_serverMutex);
    return m_currentPort;
}

void ServerWorker::setupServerConnections() {
    if ( !m_tcpServer ) {
        return;
    }

    // 仅连接TCP服务器信号
    connect(m_tcpServer, &TcpServer::newClientConnection,
        this, &ServerWorker::onNewConnection, Qt::QueuedConnection);
    connect(m_tcpServer, &TcpServer::serverStopped,
        this, &ServerWorker::onServerStopped, Qt::QueuedConnection);
    connect(m_tcpServer, &TcpServer::errorOccurred,
        this, &ServerWorker::onServerError, Qt::QueuedConnection);
}

void ServerWorker::disconnectServerSignals() {
    if ( m_tcpServer ) {
        disconnect(m_tcpServer, nullptr, this, nullptr);
    }
}

// ============================================================================
// 槽函数实现：TcpServer 事件处理
// ============================================================================

void ServerWorker::onNewConnection(qintptr socketDescriptor) {
    qCDebug(lcServer, "新客户端连接: %lld", static_cast<long long>(socketDescriptor));

    // 转发socketDescriptor到ServerManager
    emit newClientConnection(socketDescriptor);
}

void ServerWorker::onServerStopped() {
    qCDebug(lcServer, "TCP服务器已停止");

    QMutexLocker locker(&m_serverMutex);
    m_isServerRunning = false;
    m_currentPort = 0;

    emit serverStopped();
}

void ServerWorker::onServerError(const QString& error) {
    qCDebug(lcServer, "TCP服务器错误: %s", error.toUtf8().constData());
    emit serverError(error);
}

void ServerWorker::onStopTimeout() {
    qCWarning(lcServer, "停止服务器超时，强制停止");
}
