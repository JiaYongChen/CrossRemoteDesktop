#include "tcpserver.h"
#include "clienthandler.h"
#include "../common/core/protocol.h"
#include "../common/core/logger.h"
#include "../common/core/compression.h"
#include "../common/core/networkconstants.h"
#include "inputsimulator.h"
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>
#include <QtCore/QTimer>
#include <QtCore/QMutexLocker>
#include <QtCore/QDebug>
#include "../common/core/logging_categories.h"
#include <QtCore/QDateTime>
#include <QtCore/QBuffer>
#include <QtGui/QPixmap>
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMessageLogger>

TcpServer::TcpServer(QObject *parent)
    : QTcpServer(parent)
    , m_isRunning(false)
    , m_serverPort(0)
    , m_serverAddress(QHostAddress::Any)
{
    // 基础服务器初始化
}

TcpServer::~TcpServer()
{
    stopServer();
}

bool TcpServer::startServer(quint16 port, const QHostAddress &address)
{
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServer) << "TcpServer::startServer() called with port:" << port << "address:" << address.toString();
    
    if (m_isRunning) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServer) << "Server already running, returning false";
        return false;
    }
    
    m_serverAddress = address;
    
    if (!listen(address, port)) {
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
    
    emit serverStarted();
    return true;
}

void TcpServer::stopServer()
{
    stopServer(false); // 默认异步停止
}

void TcpServer::stopServer(bool synchronous)
{
    if (!m_isRunning) {
        return;
    }
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServer) << "Stopping server, synchronous:" << synchronous;
    
    auto cleanup = [this]() {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcServer) << "Starting server cleanup...";
        
        // 关闭服务器监听
        close();
        
        m_isRunning = false;
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServer) << "Server stopped successfully";
        emit serverStopped();
    };
    
    if (synchronous) {
        // 同步执行清理操作，用于应用程序关闭时
        close();
        m_isRunning = false;
        emit serverStopped();
    } else {
        // 使用定时器异步执行清理操作，避免阻塞UI线程
        QTimer::singleShot(0, this, cleanup);
    }
}

bool TcpServer::isRunning() const
{
    return m_isRunning;
}

quint16 TcpServer::serverPort() const
{
    return m_serverPort;
}

QHostAddress TcpServer::serverAddress() const
{
    return m_serverAddress;
}

void TcpServer::incomingConnection(qintptr socketDescriptor)
{
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcNetServer) << "incomingConnection descriptor:" << socketDescriptor;
    
    // 发出新连接信号，让 ServerManager 处理客户端管理
    emit newClientConnection(socketDescriptor);
}