#include "TcpServer.h"
#include "../../common/core/network/Protocol.h"
#include "../../common/core/config/NetworkConstants.h"
#include "../simulator/InputSimulator.h"
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>
#include <QtCore/QTimer>
#include <QtCore/QMutexLocker>
#include <QtCore/QDebug>
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QDateTime>
#include <QtCore/QBuffer>
#include <QtGui/QPixmap>
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMessageLogger>

TcpServer::TcpServer(QObject* parent)
    : QTcpServer(parent)
    , m_isRunning(false)
    , m_serverPort(0)
    , m_serverAddress(QHostAddress::Any) {
    // 基础服务器初始化
}

TcpServer::~TcpServer() {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcServer) << "TcpServer destructor called";

    // 析构时使用同步停止，确保资源完全释放
    if ( m_isRunning ) {
        stopServer(true); // 同步停止
    }

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcServer) << "TcpServer destructor completed";
}

bool TcpServer::startServer(quint16 port, const QHostAddress& address) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServer) << "TcpServer::startServer() called with port:" << port << "address:" << address.toString();

    if ( m_isRunning ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServer) << "Server already running, returning false";
        return false;
    }

    m_serverAddress = address;

    // 设置socket选项：允许地址重用（Windows下重要）
    // 这样可以避免TIME_WAIT状态导致的端口占用问题
    setSocketDescriptor(-1); // 重置socket描述符

    if ( !listen(address, port) ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServer) << "Failed to start server:" << errorString();
        emit errorOccurred(errorString());
        return false;
    }

    // 获取实际监听的端口（当传入端口为0时，系统会自动分配）
    m_serverPort = QTcpServer::serverPort();

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServer) << "Server successfully started on port:" << m_serverPort
        << "address:" << serverAddress().toString()
        << "listening:" << isListening()
        << "maxPending:" << maxPendingConnections();
    m_isRunning = true;
    return true;
}

void TcpServer::stopServer() {
    stopServer(false); // 默认异步停止
}

void TcpServer::stopServer(bool synchronous) {
    if ( !m_isRunning ) {
        return;
    }

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServer) << "Stopping server, synchronous:" << synchronous;

    auto cleanup = [this]() {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcServer) << "Starting server cleanup...";

        // 1. 停止接受新连接
        pauseAccepting();

        // 2. 关闭服务器监听
        close();

        // 3. 强制刷新网络缓冲区（Windows特定）
        QCoreApplication::processEvents();

        // 4. 短暂延迟确保系统释放端口（Windows下TIME_WAIT状态）
        QThread::msleep(100);

        m_isRunning = false;
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServer) << "Server stopped successfully, port should be released";
        emit serverStopped();
    };

    if ( synchronous ) {
        // 同步执行清理操作，用于应用程序关闭时
        pauseAccepting();
        close();

        // Windows下需要短暂延迟以确保端口释放
        QCoreApplication::processEvents();
        QThread::msleep(100);

        m_isRunning = false;
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServer) << "Server stopped synchronously, port released";
        emit serverStopped();
    } else {
        // 使用定时器异步执行清理操作，避免阻塞UI线程
        QTimer::singleShot(0, this, cleanup);
    }
}

bool TcpServer::isRunning() const {
    return m_isRunning;
}

quint16 TcpServer::serverPort() const {
    return m_serverPort;
}

QHostAddress TcpServer::serverAddress() const {
    return m_serverAddress;
}

void TcpServer::incomingConnection(qintptr socketDescriptor) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcNetServer) << "incomingConnection descriptor:" << socketDescriptor;

    // 发出新连接信号，让 ServerManager 处理客户端管理
    emit newClientConnection(socketDescriptor);
}