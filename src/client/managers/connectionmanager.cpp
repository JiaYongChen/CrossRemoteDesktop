#include "connectionmanager.h"
#include <QtCore/QDebug>
#include "../../common/core/logging_categories.h"
#include <QtCore/QMessageLogger>
#include <QtCore/QTimer>
#include "../tcpclient.h"

ConnectionManager::ConnectionManager(QObject *parent)
    : QObject(parent)
    , m_tcpClient(nullptr)
    , m_connectionState(Disconnected)
    , m_currentPort(0)
    , m_connectionTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_autoReconnect(false)
    , m_reconnectInterval(DEFAULT_RECONNECT_INTERVAL)
    , m_maxReconnectAttempts(DEFAULT_MAX_RECONNECT_ATTEMPTS)
    , m_currentReconnectAttempts(0)
    , m_connectionTimeout(CONNECTION_TIMEOUT)
{
    setupTcpClient();
    
    // 设置连接超时定时器
    m_connectionTimer->setSingleShot(true);
    m_connectionTimer->setInterval(m_connectionTimeout);
    connect(m_connectionTimer, &QTimer::timeout, this, &ConnectionManager::onConnectionTimeout);
    
    // 设置重连定时器
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ConnectionManager::onReconnectTimer);
}

ConnectionManager::~ConnectionManager()
{
    cleanupConnection();
}

void ConnectionManager::connectToHost(const QString &host, int port)
{
    if (m_connectionState != Disconnected) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "ConnectionManager: Already connecting or connected, disconnecting first";
        disconnectFromHost();
    }
    
    m_currentHost = host;
    m_currentPort = port;
    
    setConnectionState(Connecting);
    
    // 启动连接超时定时器
    m_connectionTimer->start();
    
    // 发起连接
    m_tcpClient->connectToHost(host, port);
}

void ConnectionManager::disconnectFromHost()
{
    if (m_connectionState == Disconnected) {
        return;
    }
    
    // 停止自动重连
    stopAutoReconnect();
    m_currentReconnectAttempts = 0;
    
    setConnectionState(Disconnecting);
    
    // 停止超时定时器
    m_connectionTimer->stop();
    
    // 断开TCP连接
    if (m_tcpClient) {
        m_tcpClient->disconnectFromHost();
    }
}

void ConnectionManager::abort()
{
    m_connectionTimer->stop();
    
    if (m_tcpClient) {
        m_tcpClient->abort();
    }
    
    cleanupConnection();
    setConnectionState(Disconnected);
}

ConnectionManager::ConnectionState ConnectionManager::connectionState() const
{
    return m_connectionState;
}

bool ConnectionManager::isConnected() const
{
    return m_connectionState == Connected || m_connectionState == Authenticated;
}

bool ConnectionManager::isAuthenticated() const
{
    return m_connectionState == Authenticated;
}

QString ConnectionManager::currentHost() const
{
    return m_currentHost;
}

int ConnectionManager::currentPort() const
{
    return m_currentPort;
}

QString ConnectionManager::sessionId() const
{
    return m_tcpClient ? m_tcpClient->sessionId() : QString();
}

TcpClient* ConnectionManager::tcpClient() const
{
    return m_tcpClient;
}

// 自动重连管理方法
void ConnectionManager::setAutoReconnect(bool enable)
{
    m_autoReconnect = enable;
    if (!enable) {
        stopAutoReconnect();
        m_currentReconnectAttempts = 0;
    }
}

bool ConnectionManager::autoReconnect() const
{
    return m_autoReconnect;
}

void ConnectionManager::setReconnectInterval(int msecs)
{
    m_reconnectInterval = qMax(1000, msecs); // 最小1秒
}

int ConnectionManager::reconnectInterval() const
{
    return m_reconnectInterval;
}

void ConnectionManager::setMaxReconnectAttempts(int attempts)
{
    m_maxReconnectAttempts = qMax(0, attempts);
}

int ConnectionManager::maxReconnectAttempts() const
{
    return m_maxReconnectAttempts;
}

int ConnectionManager::currentReconnectAttempts() const
{
    return m_currentReconnectAttempts;
}

void ConnectionManager::startAutoReconnect()
{
    if (!m_autoReconnect || m_currentReconnectAttempts >= m_maxReconnectAttempts) {
        return;
    }
    
    m_currentReconnectAttempts++;
    m_reconnectTimer->setInterval(m_reconnectInterval);
    m_reconnectTimer->start();
}

void ConnectionManager::stopAutoReconnect()
{
    m_reconnectTimer->stop();
}

void ConnectionManager::onReconnectTimer()
{
    if (m_connectionState != Disconnected && m_connectionState != Error) {
        return;
    }
    
    if (!m_currentHost.isEmpty() && m_currentPort > 0) {
        connectToHost(m_currentHost, m_currentPort);
    }
}

void ConnectionManager::onTcpConnected()
{
    m_connectionTimer->stop();
    stopAutoReconnect();
    m_currentReconnectAttempts = 0; // 重置重连计数
    setConnectionState(Connected);
    emit connected();
}

void ConnectionManager::onTcpDisconnected()
{
    m_connectionTimer->stop();
    cleanupConnection();
    setConnectionState(Disconnected);
    
    // 如果启用了自动重连且未达到最大重连次数，则启动重连
    if (m_autoReconnect && m_currentReconnectAttempts < m_maxReconnectAttempts) {
        startAutoReconnect();
    } else {
        // 重置重连计数
        m_currentReconnectAttempts = 0;
        emit disconnected();
    }
}

void ConnectionManager::onTcpAuthenticated()
{
    stopAutoReconnect();
    m_currentReconnectAttempts = 0; // 重置重连计数
    setConnectionState(Authenticated);
    emit authenticated();
}

void ConnectionManager::onTcpAuthenticationFailed(const QString &reason)
{
    setConnectionState(Error);
    emit authenticationFailed(reason);
}

void ConnectionManager::onTcpError(const QString &error)
{
    m_connectionTimer->stop();
    setConnectionState(Error);
    
    // 如果启用了自动重连且未达到最大重连次数，则启动重连
    if (m_autoReconnect && m_currentReconnectAttempts < m_maxReconnectAttempts) {
        startAutoReconnect();
    } else {
        // 重置重连计数
        m_currentReconnectAttempts = 0;
        emit errorOccurred(error);
    }
}

void ConnectionManager::onConnectionTimeout()
{
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "ConnectionManager: Connection timeout";
    setConnectionState(Error);
    
    if (m_tcpClient) {
        m_tcpClient->abort();
    }
    
    // 如果启用了自动重连且未达到最大重连次数，则启动重连
    if (m_autoReconnect && m_currentReconnectAttempts < m_maxReconnectAttempts) {
        startAutoReconnect();
    } else {
        // 重置重连计数
        m_currentReconnectAttempts = 0;
        emit errorOccurred(tr("连接超时"));
    }
}

void ConnectionManager::setConnectionState(ConnectionState state)
{
    if (m_connectionState != state) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << "ConnectionManager: State changed from" << m_connectionState << "to" << state;
        m_connectionState = state;
        emit connectionStateChanged(state);
    }
}

void ConnectionManager::setupTcpClient()
{
    m_tcpClient = new TcpClient(this);
    
    // 连接TCP客户端信号
    connect(m_tcpClient, &TcpClient::connected, this, &ConnectionManager::onTcpConnected);
    connect(m_tcpClient, &TcpClient::disconnected, this, &ConnectionManager::onTcpDisconnected);
    connect(m_tcpClient, &TcpClient::authenticated, this, &ConnectionManager::onTcpAuthenticated);
    connect(m_tcpClient, &TcpClient::authenticationFailed, this, &ConnectionManager::onTcpAuthenticationFailed);
    connect(m_tcpClient, &TcpClient::errorOccurred, this, &ConnectionManager::onTcpError);
}

void ConnectionManager::cleanupConnection()
{
    m_connectionTimer->stop();
    stopAutoReconnect();
    m_currentReconnectAttempts = 0;
    
    m_currentHost.clear();
    m_currentPort = 0;
}

// 设置连接超时
void ConnectionManager::setConnectionTimeout(int msecs)
{
    // 最小1秒，避免过小值导致误判
    m_connectionTimeout = qMax(1000, msecs);
    if (m_connectionTimer) {
        m_connectionTimer->setInterval(m_connectionTimeout);
    }
}

int ConnectionManager::connectionTimeout() const
{
    return m_connectionTimeout;
}