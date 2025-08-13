#include "servermanager.h"
#include "tcpserver.h"
#include "screencapture.h"
#include "../common/core/uiconstants.h"
#include "../common/core/protocol.h"

#include <QSettings>
#include <QTimer>
#include <QSystemTrayIcon>
#include <QMessageBox>
#include <QDebug>
#include <QMutexLocker>
#include <QDateTime>
#include <QElapsedTimer>
#include <QBuffer>
#include <QTcpSocket>
#include <QThread>
#include <QCoreApplication>
#include <cstring>

ServerManager::ServerManager(QObject *parent)
    : QObject(parent)
    , m_tcpServer(nullptr)
    , m_screenCapture(nullptr)
    , m_settings(nullptr)
    , m_stopTimeoutTimer(nullptr)
    , m_cleanupTimer(nullptr)
    , m_isServerRunning(false)
    , m_currentPort(0)
    , m_clientsMutex()
    , m_maxClients(UIConstants::DEFAULT_MAX_CLIENTS)
    , m_password("")
    , m_allowMultipleClients(false)
    , m_totalBytesReceived(0)
    , m_totalBytesSent(0)
    , m_performanceOptimizationEnabled(false)
    , m_regionDetectionEnabled(false)
    , m_advancedEncodingEnabled(false)
{
    // 创建TCP服务器
    m_tcpServer = new TcpServer(this);
    
    // 创建停止超时定时器
    m_stopTimeoutTimer = new QTimer(this);
    m_stopTimeoutTimer->setSingleShot(true);
    m_stopTimeoutTimer->setInterval(5000); // 5秒超时
    connect(m_stopTimeoutTimer, &QTimer::timeout, this, &ServerManager::onStopTimeout);
    
    // 创建客户端清理定时器
    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setSingleShot(false);
    m_cleanupTimer->setInterval(UIConstants::CLEANUP_TIMER_INTERVAL); // 每分钟清理一次
    connect(m_cleanupTimer, &QTimer::timeout, this, &ServerManager::cleanupDisconnectedClients);

    // 设置服务器连接
    setupServerConnections();
}

ServerManager::~ServerManager()
{
    disconnect(m_stopTimeoutTimer, &QTimer::timeout, this, &ServerManager::onStopTimeout);
    disconnect(m_cleanupTimer, &QTimer::timeout, this, &ServerManager::cleanupDisconnectedClients);
    
    // 停止清理定时器
    if (m_cleanupTimer) {
        m_cleanupTimer->stop();
    }

    // 同步停止服务器
    if (m_isServerRunning && m_tcpServer) {
        m_tcpServer->stopServer(true);
    }
    
    // 清理所有客户端连接
    {
        QMutexLocker locker(&m_clientsMutex);
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it.value()) {
                it.value()->forceDisconnect();
                it.value()->deleteLater();
            }
        }
        m_clients.clear();
    }

    // 停止屏幕捕获信号槽连接
    if (m_screenCapture && m_screenCapture->isCapturing()) {
        disconnect(m_screenCapture, &ScreenCapture::frameReady, this, &ServerManager::onFrameReady);
    }
    
    // 断开服务器信号连接
    disconnectServerSignals();
}

bool ServerManager::startServer()
{
    if (m_isServerRunning) {
        return true;
    }
    
    if (!m_tcpServer) {
        emit serverError(tr("服务器组件未初始化。"));
        return false;
    }
    
    // 获取服务器端口设置
    quint16 basePort = 5900; // 默认端口
    if (m_settings) {
        basePort = m_settings->value("Connection/defaultPort", 5900).toUInt();
        basePort = m_settings->value("server/port", basePort).toUInt();
    }
    
    // 尝试启动服务器
    QStringList triedPorts;
    bool serverStarted = false;
    
    for (int i = 0; i < 10; ++i) {
        quint16 port = basePort + i;
        triedPorts << QString::number(port);
        
        emit serverStatusMessage(tr("正在尝试启动服务器，端口: %1...").arg(port));
        
        if (m_tcpServer->startServer(port)) {
            m_currentPort = m_tcpServer->serverPort();
            serverStarted = true;
            
            // 保存成功启动的端口
            if (m_settings) {
                m_settings->setValue("Connection/defaultPort", port);
                m_settings->setValue("server/port", port);
            }
            break;
        }
    }
    
    if (serverStarted) {
        // 启动客户端清理定时器
        m_cleanupTimer->start();
    }
    
    if (!serverStarted) {
        QString errorMsg = tr("无法启动服务器。\n已尝试端口: %1\n\n可能的原因:\n")
                          .arg(triedPorts.join(", "));
        errorMsg += tr("• 端口被其他程序占用\n");
        errorMsg += tr("• 防火墙阻止了连接\n");
        errorMsg += tr("• 权限不足\n\n");
        errorMsg += tr("建议:\n");
        errorMsg += tr("• 检查端口占用情况\n");
        errorMsg += tr("• 关闭防火墙或添加例外\n");
        errorMsg += tr("• 以管理员权限运行\n");
        errorMsg += tr("• 在设置中选择其他端口范围");
        
        emit serverError(errorMsg);
        emit serverStatusMessage(tr("服务器启动失败"));
        return false;
    }

    return true;
}

void ServerManager::stopServer(bool synchronous)
{
    if (!m_isServerRunning || !m_tcpServer) {
        return;
    }
    
    emit serverStatusMessage(tr("正在停止服务器..."));
    
    // 停止清理定时器
    m_cleanupTimer->stop();
    
    if (synchronous) {
        // 同步停止
        m_tcpServer->stopServer(true);
        m_isServerRunning = false;
    } else {
        // 异步停止
        m_stopTimeoutTimer->start();
        m_tcpServer->stopServer(false);
    }
}

bool ServerManager::isServerRunning() const
{
    return m_isServerRunning;
}

quint16 ServerManager::getCurrentPort() const
{
    return m_currentPort;
}

void ServerManager::setSettings(QSettings *settings)
{
    m_settings = settings;
}

ScreenCapture* ServerManager::getScreenCapture() const
{
    return m_screenCapture;
}

void ServerManager::applyScreenCaptureSettings()
{
    if (!m_screenCapture || !m_settings) {
        return;
    }
    
    // 从设置中读取帧率和捕获质量
    m_settings->beginGroup("Display");
    int frameRate = m_settings->value("frameRate", 60).toInt();
    double captureQuality = m_settings->value("captureQuality", 0.8).toDouble();
    m_settings->endGroup();
    
    // 应用帧率和捕获质量设置到ScreenCapture
    m_screenCapture->setFrameRate(frameRate);
    m_screenCapture->setCaptureQuality(captureQuality);
}

void ServerManager::checkAutoStart()
{
    if (!m_settings) {
        return;
    }
    
    bool autoStartServer = m_settings->value("Server/autoStart", false).toBool();
    if (autoStartServer) {
        // 延迟启动服务器，确保UI完全初始化
        QTimer::singleShot(1000, this, &ServerManager::startServer);
    }
}

bool ServerManager::hasConnectedClients() const
{
    QMutexLocker locker(&m_clientsMutex);
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value() && it.value()->isConnected()) {
            return true;
        }
    }
    return false;
}

bool ServerManager::hasAuthenticatedClients() const
{
    QMutexLocker locker(&m_clientsMutex);
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value() && it.value()->isConnected() && it.value()->isAuthenticated()) {
            return true;
        }
    }
    return false;
}

void ServerManager::sendScreenData(const QPixmap &frame)
{
    if (!m_isServerRunning || frame.isNull()) {
        return;
    }
    
    static int sendCount = 0;
    sendCount++;
    
    QElapsedTimer timer;
    timer.start();
    
    // 压缩图像数据
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    
    // 使用JPEG格式压缩，质量设置为75%
    bool success = frame.save(&buffer, "JPEG", 75);
    if (!success) {
        qWarning() << "Failed to compress screen data";
        return;
    }
    
    buffer.close();
    
    // 发送给所有已认证的客户端
    sendMessageToAllClients(MessageType::SCREEN_DATA, imageData);
    
    if (sendCount % 30 == 0) {
        qDebug() << "ServerManager::sendScreenData completed (count:" << sendCount << "), data size:" << imageData.size();
    }
}

void ServerManager::onFrameReady(const QPixmap &frame)
{
    // 减少调试日志输出以提高性能
    static int frameCount = 0;
    frameCount++;
    
    qDebug() << "ServerManager::onFrameReady called! Frame count:" << frameCount;
    
    // 每10帧输出一次调试信息以便更好地观察问题
    if (frameCount % 10 == 0) {
        qDebug() << "ServerManager::onFrameReady - Frame captured (count:" << frameCount << "), size:" << frame.size() << "isNull:" << frame.isNull();
        qDebug() << "ServerManager::onFrameReady - Server running:" << m_isServerRunning 
                 << "Has connected clients:" << hasConnectedClients()
                 << "Has authenticated clients:" << hasAuthenticatedClients();
    }
    
    // 当有已认证的客户端连接且服务器运行时，发送屏幕数据
    if (m_isServerRunning && hasAuthenticatedClients()) {
        if (frameCount % 10 == 0) {
            qDebug() << "ServerManager::onFrameReady - Sending screen data to authenticated clients";
        }
        sendScreenData(frame);
    } else {
        // 每次都输出日志以便调试
        if (frameCount % 10 == 0) {
            qDebug() << "ServerManager::onFrameReady - NOT sending screen data:" 
                     << "Server running:" << m_isServerRunning 
                     << "Has connected clients:" << hasConnectedClients()
                     << "Has authenticated clients:" << hasAuthenticatedClients();
        }
    }
}

void ServerManager::onServerStarted()
{
    m_isServerRunning = true;
    
    // 确保从TcpServer获取最新的端口号
    if (m_tcpServer) {
        m_currentPort = m_tcpServer->serverPort();
    }
    
    emit serverStatusMessage(tr("服务器启动成功，端口: %1").arg(m_currentPort));
}

void ServerManager::onServerStopped()
{
    m_stopTimeoutTimer->stop();
    m_isServerRunning = false;
    m_currentPort = 0;
    
    // 停止屏幕捕获
    stopScreenCapture();
    
    emit serverStatusMessage(tr("服务器已停止"));
}

void ServerManager::onClientConnected(const QString &clientAddress)
{
    emit clientStatusMessage(tr("客户端已连接: %1 (等待认证)").arg(clientAddress));
    emit clientConnected(clientAddress);
}

void ServerManager::onClientDisconnected(const QString &clientAddress)
{
    emit clientStatusMessage(tr("客户端已断开: %1").arg(clientAddress));
    
    // 如果服务器仍在运行但没有其他认证客户端，停止屏幕截取功能
    if (isServerRunning() && m_screenCapture && m_screenCapture->isCapturing() && !hasAuthenticatedClients()) {
        qDebug() << "Stopping screen capture after last client disconnection";
        m_screenCapture->stopCapture();
    }
    
    emit clientDisconnected(clientAddress);
}

void ServerManager::onClientAuthenticated(const QString &clientAddress)
{
    emit clientStatusMessage(tr("客户端认证成功: %1").arg(clientAddress));
    
    qDebug() << "ServerManager::onClientAuthenticated() called for client:" << clientAddress;
    
    // 客户端认证成功后启动屏幕捕获
    if (m_screenCapture && !m_screenCapture->isCapturing()) {
        qDebug() << "Starting screen capture after client authentication...";
        startScreenCapture();
    }
    
    emit clientAuthenticated(clientAddress);
}

void ServerManager::onServerError(const QString &error)
{
    emit serverError(error);
}

void ServerManager::onNewConnection(qintptr socketDescriptor)
{
    qDebug() << "[DEBUG] ServerManager::onNewConnection called with descriptor:" << socketDescriptor;
    qDebug() << "[DEBUG] Current thread ID:" << QThread::currentThreadId();
    
    // 获取当前客户端数量
    int currentClientCount = clientCount();
    qDebug() << "[DEBUG] Current client count:" << currentClientCount;
    qDebug() << "[DEBUG] Allow multiple clients:" << m_allowMultipleClients;
    qDebug() << "[DEBUG] Max clients:" << m_maxClients;
    
    // 检查客户端数量限制
    if (!m_allowMultipleClients && currentClientCount >= 1) {
        qDebug() << "[DEBUG] Rejecting connection - multiple clients not allowed";
        sendConnectionRejectionMessage(socketDescriptor, tr("服务器不允许多个客户端同时连接"));
        return;
    }
    
    if (currentClientCount >= m_maxClients) {
        qDebug() << "[DEBUG] Rejecting connection - max clients reached:" << m_maxClients;
        sendConnectionRejectionMessage(socketDescriptor, tr("服务器已达到最大连接数限制 (%1)").arg(m_maxClients));
        return;
    }
    
    // 创建客户端处理器
    ClientHandler *handler = new ClientHandler(socketDescriptor, this);
    qDebug() << "[DEBUG] Created ClientHandler for client:" << handler->clientId();
    
    // 连接信号到 ServerManager
    connect(handler, &ClientHandler::connected, this, [this, handler]() {
        onClientConnected(handler->clientAddress());
    });
    connect(handler, &ClientHandler::disconnected, this, [this, handler]() {
        onClientDisconnected(handler->clientAddress());
        unregisterClientHandler(handler->clientAddress());
    });
    connect(handler, &ClientHandler::authenticated, this, [this, handler]() {
        onClientAuthenticated(handler->clientAddress());
    });
    connect(handler, &ClientHandler::messageReceived, this, [this, handler](MessageType type, const QByteArray &data) {
        onMessageReceived(handler->clientAddress(), type, data);
    });
    connect(handler, &ClientHandler::errorOccurred, this, [this](const QString &error) {
        onClientError(error);
    });
    
    // 注册客户端处理器
    registerClientHandler(handler);
    qDebug() << "[DEBUG] Client added to list. Total clients:" << clientCount();
}

void ServerManager::onStopTimeout()
{
    qDebug() << "Server stop timeout, forcing stop";
    m_isServerRunning = false;
    emit serverStatusMessage(tr("服务器停止超时，已强制停止"));
}

void ServerManager::setupServerConnections()
{
    if (!m_tcpServer) {
        return;
    }
    
    connect(m_tcpServer, &TcpServer::serverStarted, this, &ServerManager::onServerStarted);
    connect(m_tcpServer, &TcpServer::serverStopped, this, &ServerManager::onServerStopped);
    connect(m_tcpServer, &TcpServer::newConnection, this, &ServerManager::onNewConnection);
    connect(m_tcpServer, &TcpServer::errorOccurred, this, &ServerManager::onServerError);
}

void ServerManager::disconnectServerSignals()
{
    if (!m_tcpServer) {
        return;
    }
    
    disconnect(m_tcpServer, &TcpServer::serverStarted, this, &ServerManager::onServerStarted);
    disconnect(m_tcpServer, &TcpServer::serverStopped, this, &ServerManager::onServerStopped);
    disconnect(m_tcpServer, &TcpServer::errorOccurred, this, &ServerManager::onServerError);
}

void ServerManager::startScreenCapture()
{
    if (!m_screenCapture) {
        m_screenCapture = new ScreenCapture(this);
        // 设置屏幕捕获连接        
        connect(m_screenCapture, &ScreenCapture::frameReady, this, &ServerManager::onFrameReady);
    }
    m_screenCapture->startCapture();
}

void ServerManager::stopScreenCapture()
{
    if (m_screenCapture && m_screenCapture->isCapturing()) {
        m_screenCapture->stopCapture();
    }
}

// 客户端管理方法实现
int ServerManager::clientCount() const
{
    QMutexLocker locker(&m_clientsMutex);
    return m_clients.size();
}

QStringList ServerManager::connectedClients() const
{
    QMutexLocker locker(&m_clientsMutex);
    return m_clients.keys();
}

void ServerManager::setMaxClients(int maxClients)
{
    m_maxClients = maxClients;
    m_tcpServer->setMaxPendingConnections(maxClients);
}

int ServerManager::maxClients() const
{
    return m_maxClients;
}

void ServerManager::setPassword(const QString &password)
{
    m_password = password;
}

QString ServerManager::password() const
{
    return m_password;
}

void ServerManager::setAllowMultipleClients(bool allow)
{
    m_allowMultipleClients = allow;
    // 配置现在由 ServerManager 直接管理，不需要传递给 TcpServer
}

bool ServerManager::allowMultipleClients() const
{
    return m_allowMultipleClients;
}

quint64 ServerManager::totalBytesReceived() const
{
    return m_totalBytesReceived;
}

quint64 ServerManager::totalBytesSent() const
{
    return m_totalBytesSent;
}

void ServerManager::sendMessageToClient(const QString &clientAddress, MessageType type, const QByteArray &data)
{
    QMutexLocker locker(&m_clientsMutex);
    if (m_clients.contains(clientAddress)) {
        ClientHandler *handler = m_clients[clientAddress];
        if (handler) {
            handler->sendMessage(type, data);
        }
    }
}

void ServerManager::sendMessageToAllClients(MessageType type, const QByteArray &data)
{
    QMutexLocker locker(&m_clientsMutex);
    int authenticatedClients = 0;
    int totalClients = 0;
    
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value()) {
            totalClients++;
            // 对于屏幕数据，只发送给已认证的客户端
            if (type == MessageType::SCREEN_DATA) {
                if (it.value()->isAuthenticated()) {
                    authenticatedClients++;
                    it.value()->sendMessage(type, data);
                    qDebug() << "Sending screen data to authenticated client:" << it.value()->clientAddress() 
                             << "Data size:" << data.size() << "bytes";
                }
            } else {
                // 其他类型的消息发送给所有连接的客户端
                it.value()->sendMessage(type, data);
            }
        }
    }
    
    if (type == MessageType::SCREEN_DATA) {
        static int frameCount = 0;
        frameCount++;
        if (frameCount % 30 == 0) { // 每30帧输出一次统计信息
            qDebug() << "Screen data frame" << frameCount << "sent to" << authenticatedClients 
                     << "authenticated clients out of" << totalClients << "total clients";
        }
    }
}

void ServerManager::disconnectClient(const QString &clientAddress)
{
    QMutexLocker locker(&m_clientsMutex);
    if (m_clients.contains(clientAddress)) {
        ClientHandler *handler = m_clients[clientAddress];
        if (handler) {
            handler->disconnectClient();
        }
    }
}

void ServerManager::disconnectAllClients()
{
    QMutexLocker locker(&m_clientsMutex);
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value()) {
            it.value()->disconnectClient();
        }
    }
}

void ServerManager::onMessageReceived(const QString &clientAddress, MessageType type, const QByteArray &data)
{
    emit messageReceived(clientAddress, type, data);
}

void ServerManager::onClientError(const QString &error)
{
    emit clientStatusMessage(tr("客户端错误: %1").arg(error));
}

void ServerManager::cleanupDisconnectedClients()
{
    QMutexLocker locker(&m_clientsMutex);
    auto it = m_clients.begin();
    while (it != m_clients.end()) {
        if (!it.value() || !it.value()->isConnected()) {
            if (it.value()) {
                it.value()->deleteLater();
            }
            it = m_clients.erase(it);
        } else {
            ++it;
        }
    }
}

ClientHandler* ServerManager::findClientHandler(const QString &address)
{
    QMutexLocker locker(&m_clientsMutex);
    return m_clients.value(address, nullptr);
}

QString ServerManager::generateClientId(const QString &address, quint16 port)
{
    return QString("%1:%2").arg(address).arg(port);
}

void ServerManager::registerClientHandler(ClientHandler *handler)
{
    if (!handler) return;
    
    QString clientId = generateClientId(handler->clientAddress(), handler->clientPort());
    QMutexLocker locker(&m_clientsMutex);
    m_clients[clientId] = handler;
}

void ServerManager::unregisterClientHandler(const QString &clientAddress)
{
    QMutexLocker locker(&m_clientsMutex);
    auto it = m_clients.find(clientAddress);
    if (it != m_clients.end()) {
        if (it.value()) {
            it.value()->deleteLater();
        }
        m_clients.erase(it);
    }
}

// 性能统计方法实现
double ServerManager::getAverageFrameTime() const
{
    // 如果定时器未启动，返回0
    if (!m_frameTimer.isValid()) {
        return 0.0;
    }
    
    // 返回平均帧时间（毫秒）
    return m_frameTimer.elapsed() / 1000.0;
}

double ServerManager::getAverageCompressionRatio() const
{
    // 简单的压缩比计算，实际实现可能需要更复杂的逻辑
    if (m_lastFrame.isNull()) {
        return 0.0;
    }
    
    // 估算压缩比：原始大小 vs 压缩后大小
    int originalSize = m_lastFrame.width() * m_lastFrame.height() * 4; // RGBA
    int compressedSize = originalSize / 10; // 假设10:1的压缩比
    
    return static_cast<double>(originalSize) / compressedSize;
}

int ServerManager::getCurrentFrameRate() const
{
    // 简单的帧率计算，实际实现可能需要更复杂的逻辑
    if (!m_frameTimer.isValid()) {
        return 0;
    }
    
    // 假设每秒30帧
    return 30;
}

// 性能优化控制方法实现
void ServerManager::enablePerformanceOptimization(bool enabled)
{
    m_performanceOptimizationEnabled = enabled;
    
    // 如果TCP服务器存在，将设置传递给它
    if (m_tcpServer) {
        // 注意：这里需要在TcpServer中实现相应的方法，或者通过其他方式传递设置
        qDebug() << "Performance optimization" << (enabled ? "enabled" : "disabled");
    }
}

void ServerManager::enableRegionDetection(bool enabled)
{
    m_regionDetectionEnabled = enabled;
    
    // 如果TCP服务器存在，将设置传递给它
    if (m_tcpServer) {
        qDebug() << "Region detection" << (enabled ? "enabled" : "disabled");
    }
}

void ServerManager::enableAdvancedEncoding(bool enabled)
{
    m_advancedEncodingEnabled = enabled;
    
    // 如果TCP服务器存在，将设置传递给它
    if (m_tcpServer) {
        qDebug() << "Advanced encoding" << (enabled ? "enabled" : "disabled");
    }
}



void ServerManager::sendConnectionRejectionMessage(qintptr socketDescriptor, const QString &errorMessage)
{
    // 创建临时socket来发送错误消息
    QTcpSocket *socket = new QTcpSocket();
    
    // 设置socket描述符
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        qWarning() << "Failed to set socket descriptor for rejection message";
        socket->deleteLater();
        return;
    }
    
    // 创建错误消息
    ErrorMessage errorMsg;
    errorMsg.errorCode = 1001; // 连接被拒绝的错误代码
    
    // 初始化整个errorText数组为零，避免垃圾数据
    memset(errorMsg.errorText, 0, sizeof(errorMsg.errorText));
    
    QByteArray errorText = errorMessage.toUtf8();
    int copySize = qMin(errorText.size(), static_cast<int>(sizeof(errorMsg.errorText) - 1));
    memcpy(errorMsg.errorText, errorText.constData(), copySize);
    errorMsg.errorText[copySize] = '\0';
    
    // 序列化错误消息
    QByteArray errorData = Protocol::serialize(errorMsg);
    
    // 创建完整的协议消息
    QByteArray message = Protocol::createMessage(MessageType::ERROR_MESSAGE, errorData);
    
    // 发送错误消息
    qint64 bytesWritten = socket->write(message);
    socket->flush();
    
    qDebug() << "Sent connection rejection message:" << errorMessage << "bytes written:" << bytesWritten;
    
    // 等待更长时间确保数据发送完成后断开连接
    QTimer::singleShot(500, [socket]() {
        if (socket->state() == QAbstractSocket::ConnectedState) {
            socket->disconnectFromHost();
            // 如果断开连接需要时间，等待一段时间后强制关闭
            QTimer::singleShot(1000, [socket]() {
                if (socket->state() != QAbstractSocket::UnconnectedState) {
                    socket->abort();
                }
                socket->deleteLater();
            });
        } else {
            socket->deleteLater();
        }
    });
}