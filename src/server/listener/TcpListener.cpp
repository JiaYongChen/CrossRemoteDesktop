// src/server/listener/TcpListener.cpp
#include "server/listener/TcpListener.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>
#include <QtCore/QTimer>

#include "common/error/ErrorCode.h"
#include "common/error/RdError.h"
#include "common/logging/LoggingCategories.h"
#include "server/service/TcpServer.h"

TcpListener::TcpListener(QObject* parent)
    : Worker(parent)
    , m_tcpServer(nullptr)
    , m_stopTimeoutTimer(nullptr)
    , m_serverMutex()
    , m_isRunning(false)
    , m_currentPort(0) {
    setName("TcpListener");
    qCDebug(lcServer) << "TcpListener created";
}

TcpListener::~TcpListener() {
    qCDebug(lcServer) << "TcpListener destroyed";
    if (m_isRunning) {
        stopListening();
    }
}

bool TcpListener::initialize() {
    qCDebug(lcServer) << "TcpListener::initialize()";

    m_tcpServer = new TcpServer(this);
    m_stopTimeoutTimer = new QTimer(this);
    m_stopTimeoutTimer->setSingleShot(true);
    m_stopTimeoutTimer->setInterval(5000);
    connect(m_stopTimeoutTimer, &QTimer::timeout, this, &TcpListener::onStopTimeout);
    setupServerConnections();

    qCInfo(lcServer) << "TcpListener initialized";
    return true;
}

void TcpListener::cleanup() {
    qCDebug(lcServer) << "TcpListener::cleanup()";

    if (m_stopTimeoutTimer) {
        m_stopTimeoutTimer->stop();
    }
    disconnectServerSignals();

    if (m_tcpServer) {
        m_tcpServer->stopServer(true);
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }

    qCInfo(lcServer) << "TcpListener cleaned up";
}

void TcpListener::processTask() {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    QThread::msleep(1);
}

void TcpListener::startListening(quint16 port) {
    QMutexLocker locker(&m_serverMutex);

    if (m_isRunning) {
        qCDebug(lcServer) << "TcpListener already listening";
        return;
    }

    qCDebug(lcServer) << "TcpListener starting on port:" << port;

    if (!m_tcpServer) {
        qCWarning(lcServer) << "TcpServer not initialized";
        emit errorOccurred(RdError(ErrorCode::ServerStartFailed,
                                   tr("TCP服务器未初始化"), "TcpListener"));
        return;
    }

    bool result = m_tcpServer->startServer(port);
    if (result) {
        m_currentPort = m_tcpServer->serverPort();
        m_isRunning = true;
        emit listening(m_currentPort);
        qCInfo(lcServer) << "TcpListener listening on port:" << m_currentPort;
    } else {
        emit errorOccurred(RdError(ErrorCode::ServerStartFailed,
                                   tr("服务器启动失败"), "TcpListener"));
        qCWarning(lcServer) << "TcpListener start failed";
    }
}

void TcpListener::stopListening() {
    QMutexLocker locker(&m_serverMutex);

    if (!m_isRunning) {
        qCDebug(lcServer) << "TcpListener not listening, skip stop";
        return;
    }

    qCDebug(lcServer) << "TcpListener stopping";

    if (m_tcpServer) {
        m_tcpServer->stopServer(false);
    }

    m_isRunning = false;
    m_currentPort = 0;
    emit stopped();
    qCInfo(lcServer) << "TcpListener stopped";
}

bool TcpListener::isListening() const {
    QMutexLocker locker(&m_serverMutex);
    return m_isRunning;
}

quint16 TcpListener::port() const {
    QMutexLocker locker(&m_serverMutex);
    return m_currentPort;
}

QSslCertificate TcpListener::sslCertificate() const {
    if (m_tcpServer) {
        return m_tcpServer->sslCertificate();
    }
    return QSslCertificate();
}

QSslKey TcpListener::sslPrivateKey() const {
    if (m_tcpServer) {
        return m_tcpServer->sslPrivateKey();
    }
    return QSslKey();
}

void TcpListener::setupServerConnections() {
    if (!m_tcpServer) return;
    connect(m_tcpServer, &TcpServer::newClientConnection,
            this, &TcpListener::onNewConnection, Qt::QueuedConnection);
    connect(m_tcpServer, &TcpServer::serverStopped,
            this, &TcpListener::onServerStopped, Qt::QueuedConnection);
    connect(m_tcpServer, &TcpServer::errorOccurred,
            this, &TcpListener::onServerError, Qt::QueuedConnection);
}

void TcpListener::disconnectServerSignals() {
    if (m_tcpServer) {
        disconnect(m_tcpServer, nullptr, this, nullptr);
    }
}

void TcpListener::onNewConnection(qintptr socketDescriptor) {
    qCInfo(lcServer) << "TcpListener: new connection, descriptor:" << socketDescriptor;
    emit newConnection(socketDescriptor);
}

void TcpListener::onServerStopped() {
    qCInfo(lcServer) << "TcpListener: TCP server stopped";
    QMutexLocker locker(&m_serverMutex);
    m_isRunning = false;
    m_currentPort = 0;
    emit stopped();
}

void TcpListener::onServerError(const RdError& error) {
    qCWarning(lcServer) << "TcpListener: TCP server error:" << error.logLabel();
    emit errorOccurred(RdError(ErrorCode::TcpListenerError, error.logLabel(), "TcpListener"));
}

void TcpListener::onStopTimeout() {
    qCWarning(lcServer) << "TcpListener: stop timeout, forcing stop";
}
