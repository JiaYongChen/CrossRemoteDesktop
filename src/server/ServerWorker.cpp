#include "ServerWorker.h"
#include "TcpServer.h"
#include "capture/ScreenCapture.h"
#include "ClientHandler.h"
#include "../common/core/crypto/Encryption.h"
#include "../common/core/config/Constants.h"
#include "../common/core/network/Protocol.h"
#include "../common/core/logging/LoggingCategories.h"
// 新增：引入自适应压缩管理器与基础压缩API
#include "../common/core/compression/AdvancedCompressionManager.h"
#include "../common/core/compression/Compression.h"
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtCore/QDebug>
#include <QtCore/QMutexLocker>
#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtCore/QBuffer>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>
#include <QtCore/QMessageLogger>
#include <cstring>

ServerWorker::ServerWorker(QObject *parent)
    : Worker(parent)
    , m_tcpServer(nullptr)
    , m_screenCapture(nullptr)
    , m_stopTimeoutTimer(nullptr)
    , m_cleanupTimer(nullptr)
    , m_serverMutex()
    , m_isServerRunning(false)
    , m_currentPort(0)
    , m_currentClient(nullptr)
    , m_clientMutex()
    , m_password("")
    , m_dataProcessor(std::make_unique<DataProcessor>(this))
    , m_dataConfig(std::make_unique<DataProcessingConfig>(this))
    , m_storageManager(std::make_unique<StorageManager>(this))
{
    setName("ServerWorker");
    qCDebug(lcServer, "初始化服务器工作线程");
    qCDebug(lcServer, "数据处理模块已初始化");
    
    // 初始化存储管理器
    if (m_storageManager) {
        StorageManager::StorageConfig storageConfig;
        storageConfig.policy = StorageManager::StoragePolicy::KeyFramesOnly;
        storageConfig.maxStorageMB = 500;
        storageConfig.keyFrameIntervalSec = 10;
        storageConfig.retentionDays = 7;
        storageConfig.enableDiagnostics = true;
        
        if (m_storageManager->initialize(storageConfig)) {
            qCDebug(lcServer, "存储管理器初始化成功");
        } else {
            qCWarning(lcServer, "存储管理器初始化失败");
        }
    }
}

ServerWorker::~ServerWorker()
{
    qCDebug(lcServer, "销毁服务器工作线程");
    
    // 确保服务器已停止
    if (m_isServerRunning) {
        stopServer(true);
    }
    
    // 清理资源在cleanup()中进行
}

bool ServerWorker::initialize()
{
    qCDebug(lcServer, "初始化服务器工作线程组件");
    
    // 创建TCP服务器（必须在工作线程中创建）
    m_tcpServer = new TcpServer(this);
    
    // 创建屏幕捕获器
    m_screenCapture = new ScreenCapture(this);
    
    // 创建停止超时定时器
    m_stopTimeoutTimer = new QTimer(this);
    m_stopTimeoutTimer->setSingleShot(true);
    m_stopTimeoutTimer->setInterval(5000); // 5秒超时
    connect(m_stopTimeoutTimer, &QTimer::timeout, this, &ServerWorker::onStopTimeout);
    
    // 创建客户端清理定时器
    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setSingleShot(false);
    m_cleanupTimer->setInterval(CoreConstants::Server::CLEANUP_TIMER_INTERVAL); // 每分钟清理一次
    connect(m_cleanupTimer, &QTimer::timeout, this, &ServerWorker::cleanupDisconnectedClients);
    
    // 初始化自适应压缩管理器（使用unique_ptr管理其生命周期，不设置Qt父对象，避免重复析构）
    if (!m_acm) {
        m_acm = std::make_unique<AdvancedCompressionManager>();
        m_acm->setCompressionStrategy(AdvancedCompressionManager::AdaptiveStrategy);
        AdvancedCompressionManager::AdaptiveConfig cfg{};
        cfg.enableAdaptiveStrategy = true;        // 启用自适应策略
        cfg.enableChangeDetection = true;        // 启用变化检测
        cfg.enablePerformanceMonitoring = false; // 默认关闭性能计时，避免额外开销
        cfg.maxFrameHistory = 5;                 // 保存最近5帧用于对比
        cfg.changeThreshold = 0.15;              // 变化阈值（15%以内尝试差分）
        cfg.blockSize = 32;                      // 块大小
        cfg.performanceUpdateInterval = 1000;    // 1秒更新一次性能（如开启）
        m_acm->setAdaptiveConfig(cfg);
        qCDebug(lcServer, "AdvancedCompressionManager initialized with Adaptive strategy");
    }
    m_prevEncodedFrameData.clear(); // 初始化上一帧缓存为空
    // 设置服务器连接
    setupServerConnections();
    
    qCDebug(lcServer, "服务器工作线程初始化完成");
    return true;
}

void ServerWorker::cleanup()
{
    qCDebug(lcServer, "清理服务器工作线程资源");
    
    // 停止定时器
    if (m_stopTimeoutTimer) {
        m_stopTimeoutTimer->stop();
    }
    
    if (m_cleanupTimer) {
        m_cleanupTimer->stop();
    }
    
    // 停止屏幕捕获
    stopScreenCapture();
    
    // 清理客户端连接
    {
        QMutexLocker locker(&m_clientMutex);
        if (m_currentClient) {
            m_currentClient->forceDisconnect();
            m_currentClient->deleteLater();
            m_currentClient = nullptr;
        }
    }
    
    // 断开服务器信号连接
    disconnectServerSignals();
    
    // 停止TCP服务器
    if (m_tcpServer) {
        m_tcpServer->stopServer(true);
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }
    
    // 清理屏幕捕获器
    if (m_screenCapture) {
        m_screenCapture->deleteLater();
        m_screenCapture = nullptr;
    }
    
    qCDebug(lcServer, "服务器工作线程资源清理完成");
}

void ServerWorker::processTask()
{
    // 在工作线程中处理事件循环
    // 这个方法会被Worker基类的工作循环调用
    // 由于我们使用Qt的事件循环，这里主要是保持线程活跃
    
    try {
        // 处理Qt事件循环中的待处理事件
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        
        // 非常短暂的休眠，提高停止响应性
        QThread::msleep(1); // 减少休眠时间，提高停止响应速度
    } catch (const std::exception& e) {
        qCDebug(lcServer, "Exception in ServerWorker::processTask: %s", e.what());
        // 不重新抛出异常，优雅地处理异常，避免程序崩溃
        // 发出错误信号但继续运行
        emit errorOccurred(QString("ServerWorker processTask exception: %1").arg(e.what()));
    } catch (...) {
        qCDebug(lcServer, "Unknown exception in ServerWorker::processTask");
        // 不重新抛出异常，优雅地处理异常，避免程序崩溃
        // 发出错误信号但继续运行
        emit errorOccurred("ServerWorker processTask unknown exception");
    }
}

bool ServerWorker::startServer(quint16 port)
{
    QMutexLocker locker(&m_serverMutex);
    
    if (m_isServerRunning) {
        qCDebug(lcServer, "服务器已在运行中");
        return true;
    }
    
    qCDebug(lcServer, "启动服务器，端口: %d", port);
    
    // 确保工作线程已启动
    if (!isRunning()) {
        start();
        // 等待线程启动完成
        QThread::msleep(100);
    }
    
    // 直接在当前线程中执行，避免死锁
    bool result = false;
    if (!m_tcpServer) {
        qCDebug(lcServer, "TCP服务器未初始化");
        return false;
    }
    
    result = m_tcpServer->startServer(port);
    if (result) {
        m_currentPort = m_tcpServer->serverPort();
        m_isServerRunning = true;
        
        // 启动清理定时器
        m_cleanupTimer->start();
        
        emit serverStarted(m_currentPort);
        
        qCDebug(lcServer, "服务器启动成功，端口: %d", m_currentPort);
    } else {
        emit serverError(tr("服务器启动失败"));
        qCDebug(lcServer, "服务器启动失败");
    }
    
    return result;
}

void ServerWorker::stopServer(bool synchronous)
{
    QMutexLocker locker(&m_serverMutex);
    
    if (!m_isServerRunning) {
        qCDebug(lcServer, "服务器未运行，无需停止");
        return;
    }
    
    qCDebug(lcServer, "停止服务器，同步模式: %s", synchronous ? "true" : "false");
    
    auto stopOperation = [this]() {
        // 停止清理定时器
        if (m_cleanupTimer) {
            m_cleanupTimer->stop();
        }
        
        // 停止屏幕捕获
        stopScreenCapture();
        
        // 断开所有客户端
        {
            QMutexLocker clientLocker(&m_clientMutex);
            if (m_currentClient) {
                m_currentClient->forceDisconnect();
                m_currentClient->deleteLater();
                m_currentClient = nullptr;
            }
        }
        
        // 停止TCP服务器
        if (m_tcpServer) {
            m_tcpServer->stopServer(true);
        }
        
        m_isServerRunning = false;
        m_currentPort = 0;
        
        emit serverStopped();
        
        qCDebug(lcServer, "服务器停止完成");
    };
    
    if (synchronous) {
        // 同步执行
        QMetaObject::invokeMethod(this, stopOperation, Qt::BlockingQueuedConnection);
    } else {
        // 异步执行
        QMetaObject::invokeMethod(this, stopOperation, Qt::QueuedConnection);
    }
}

bool ServerWorker::isServerRunning() const
{
    QMutexLocker locker(&m_serverMutex);
    return m_isServerRunning;
}

quint16 ServerWorker::getCurrentPort() const
{
    QMutexLocker locker(&m_serverMutex);
    return m_currentPort;
}

void ServerWorker::setPassword(const QString &password)
{
    m_password = password;
    
    // 生成盐值和摘要
    if (!password.isEmpty()) {
   // 生成密码盐值和摘要
    m_passwordSalt = RandomGenerator::generateSalt();
    m_passwordDigest = HashGenerator::pbkdf2(password.toUtf8(), m_passwordSalt, 10000, 32);
        qCDebug(lcServer, "服务器密码已设置");
    } else {
        m_passwordSalt.clear();
        m_passwordDigest.clear();
        qCDebug(lcServer, "服务器密码已清除");
    }

    // 新增：同步更新当前客户端期望的认证摘要，确保后续认证流程正常
    {
        QMutexLocker locker(&m_clientMutex);
        if (m_currentClient) {
            m_currentClient->setExpectedPasswordDigest(m_passwordSalt, m_passwordDigest);
            qCDebug(lcServer, "已为当前客户端同步更新期望的密码摘要（长度: %lld, 盐长: %lld）",
                    static_cast<long long>(m_passwordDigest.size()),
                    static_cast<long long>(m_passwordSalt.size()));
        }
    }
}

QString ServerWorker::password() const
{
    // 为兼容性保留，但不返回明文密码
    return m_password.isEmpty() ? QString() : QString("****");
}

bool ServerWorker::hasConnectedClients() const
{
    QMutexLocker locker(&m_clientMutex);
    return m_currentClient != nullptr;
}

bool ServerWorker::hasAuthenticatedClients() const
{
    QMutexLocker locker(&m_clientMutex);
    return m_currentClient != nullptr && m_currentClient->isAuthenticated();
}

int ServerWorker::clientCount() const
{
    QMutexLocker locker(&m_clientMutex);
    return m_currentClient ? 1 : 0;
}

QStringList ServerWorker::connectedClients() const
{
    QMutexLocker locker(&m_clientMutex);
    QStringList clients;
    if (m_currentClient) {
        clients << m_currentClient->clientAddress();
    }
    return clients;
}

void ServerWorker::sendMessageToClient(MessageType type, const IMessageCodec &message)
{
    QMutexLocker locker(&m_clientMutex);
    if (m_currentClient && m_currentClient->isAuthenticated()) {
        m_currentClient->sendMessage(type, message);
    }
}

void ServerWorker::disconnectClient()
{
    QMutexLocker locker(&m_clientMutex);
    if (m_currentClient) {
        m_currentClient->forceDisconnect();
    }
}

void ServerWorker::setupServerConnections()
{
    if (!m_tcpServer) {
        return;
    }
    
    // 连接TCP服务器信号
    connect(m_tcpServer, &TcpServer::newClientConnection, 
            this, &ServerWorker::onNewConnection, Qt::QueuedConnection);
    connect(m_tcpServer, &TcpServer::serverStopped, 
            this, &ServerWorker::onServerStopped, Qt::QueuedConnection);
    connect(m_tcpServer, &TcpServer::errorOccurred, 
            this, &ServerWorker::onServerError, Qt::QueuedConnection);
    
    // 连接屏幕捕获信号
    if (m_screenCapture) {
        connect(m_screenCapture, &ScreenCapture::frameReady,
                this, &ServerWorker::onFrameReady, Qt::QueuedConnection);
    }
}

void ServerWorker::disconnectServerSignals()
{
    if (m_tcpServer) {
        disconnect(m_tcpServer, nullptr, this, nullptr);
    }
    
    if (m_screenCapture) {
        disconnect(m_screenCapture, nullptr, this, nullptr);
    }
}

void ServerWorker::startScreenCapture()
{
    if (!m_screenCapture) {
        qCDebug(lcServer, "屏幕捕获器未初始化");
        return;
    }
    
    // 连接屏幕捕获信号
    connect(m_screenCapture, &ScreenCapture::frameReady, 
            this, &ServerWorker::onFrameReady, Qt::QueuedConnection);
    
    // 将存储管理器注入到屏幕捕获工作线程
    if (m_storageManager) {
        // 注意：这里需要通过ScreenCapture访问其内部的ScreenCaptureWorker
        // 为了简化，我们先在这里记录日志，实际注入需要ScreenCapture提供接口
        qCDebug(lcServer, "准备将存储管理器注入到屏幕捕获工作线程");
    }
    
    // 启动屏幕捕获
    m_screenCapture->startCapture();
    qCDebug(lcServer, "屏幕捕获已启动");
}

void ServerWorker::stopScreenCapture()
{
    if (m_screenCapture && m_screenCapture->isCapturing()) {
        disconnect(m_screenCapture, &ScreenCapture::frameReady, 
                   this, &ServerWorker::onFrameReady);
        m_screenCapture->stopCapture();
        qCDebug(lcServer, "屏幕捕获已停止");
    }
}

void ServerWorker::sendScreenData(const QImage &frame)
{
    if (!m_currentClient) {
        qCDebug(lcServer, "No client connected, skipping screen data send");
        return;
    }

    // 【新增】第二阶段：数据处理流程
    if (m_dataConfig && m_dataConfig->isCleaningEnabled() && m_dataProcessor) {
        // 转换图像为字节数组
        QByteArray imageData;
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::WriteOnly);
        frame.save(&buffer, "PNG");
        
        // 处理和存储数据
        QString dataId, error;
        if (m_dataProcessor->processAndStore(imageData, "image/png", dataId, error)) {
            // 发送处理后的数据
            DataRecord processedRecord;
            if (m_dataProcessor->retrieve(dataId, processedRecord, error)) {
                qCDebug(lcServer, "使用数据处理模块处理帧数据，ID: %s", dataId.toUtf8().constData());
                sendProcessedImageData(processedRecord);
                return;
            } else {
                qCWarning(lcServer, "检索处理后数据失败: %s", error.toUtf8().constData());
            }
        } else {
            qCWarning(lcServer, "数据处理失败: %s，回退到原始流程", error.toUtf8().constData());
        }
    }

    try {
        // 1) 选择最优图像格式与质量（使用ACM的选择策略，但保持二进制兼容：不在数据前添加自定义帧类型字节）
        AdvancedCompressionManager::CompressionStrategy strategy = m_acm ? m_acm->compressionStrategy() : AdvancedCompressionManager::AdaptiveStrategy;
        Compression::ImageFormat format = m_acm ? m_acm->selectOptimalImageFormat(frame, strategy)
                                               : Compression::selectOptimalFormat(frame);
        int quality = m_acm ? m_acm->selectOptimalQuality(frame, format, strategy)
                            : Compression::selectOptimalQuality(frame, format);

        // 2) 编码当前完整帧为所选格式的字节数组
        QByteArray currentFullEncoded = Compression::compressImage(frame, format, quality);
        if (currentFullEncoded.isEmpty()) {
            qCDebug(lcServer, "Failed to encode frame using selected format=%s, quality=%d", Compression::imageFormatToString(format).toUtf8().constData(), quality);
            return;
        }

        // 3) 计算差分并根据效果决定是否采用差分发送
        const double diffThresholdRatio = 0.80; // 当差分数据小于完整帧80%时才采用差分
        bool useDifferential = false;
        QByteArray payload;
        if (!m_prevEncodedFrameData.isEmpty()) {
            QByteArray diff = Compression::compressDifference(currentFullEncoded, m_prevEncodedFrameData);
            if (!diff.isEmpty() && diff.size() < static_cast<int>(currentFullEncoded.size() * diffThresholdRatio)) {
                // 差分有效：客户端将调用applyDifference(previous, diff)还原完整帧
                payload = diff;
                useDifferential = true;
            } else {
                // 差分无效或不划算，直接发送完整帧
                payload = currentFullEncoded;
                useDifferential = false;
            }
        } else {
            // 首帧或无参考帧：必须发送完整帧
            payload = currentFullEncoded;
            useDifferential = false;
        }

        // 4) 构造并发送ScreenData（与客户端兼容：imageData为“完整帧字节”或“差分字节”）
        ScreenData screenData;
        screenData.x = 0;                                        // 屏幕起始X坐标
        screenData.y = 0;                                        // 屏幕起始Y坐标
        screenData.width = static_cast<quint16>(frame.width());  // 图像宽度
        screenData.height = static_cast<quint16>(frame.height());// 图像高度
        screenData.imageType = static_cast<quint8>(format);      // 图像类型：与Compression::ImageFormat枚举一致
        screenData.compressionType = useDifferential ? 1 : 0;    // 压缩类型：0=完整帧，1=差分帧（客户端目前忽略该字段，但保留以便扩展）
        screenData.dataSize = static_cast<quint32>(payload.size()); // 数据大小
        screenData.imageData = payload;                          // 图像数据

        // 发送屏幕数据消息给客户端
        sendMessageToClient(MessageType::SCREEN_DATA, screenData);

        // 5) 更新上一帧完整编码数据（用于下次差分参考）。注意：即使本次发送差分，也要保存完整帧
        m_prevEncodedFrameData = currentFullEncoded;

        qCDebug(lcServer, "Screen data sent: %dx%d, fmt=%s, quality=%d, mode=%s, payload=%lld bytes (full=%lld bytes)",
                frame.width(), frame.height(),
                Compression::imageFormatToString(format).toUtf8().constData(), quality,
                useDifferential ? "diff" : "full",
                static_cast<long long>(payload.size()),
                static_cast<long long>(currentFullEncoded.size()));
    } catch (const std::exception &e) {
        qCDebug(lcServer, "Exception occurred while sending screen data: %s", e.what());
    } catch (...) {
        qCDebug(lcServer, "Unknown exception occurred while sending screen data");
    }
}

void ServerWorker::onNewConnection(qintptr socketDescriptor)
{
    qCDebug(lcServer, "新客户端连接: %lld", static_cast<long long>(socketDescriptor));
    
    QMutexLocker locker(&m_clientMutex);
    
    // 单连接模式：如果已有客户端连接，拒绝新连接
    if (m_currentClient) {
        qCDebug(lcServer, "已有客户端连接，拒绝新连接");
        sendConnectionRejectionMessage(socketDescriptor, tr("服务器已有客户端连接"));
        return;
    }
    
    // 创建客户端处理器
    m_currentClient = new ClientHandler(socketDescriptor, this);

    // 新增：在连接建立后立刻下发服务端期望的密码盐与摘要，避免客户端首次认证时拿不到挑战参数
    // 若服务端未设置密码（盐/摘要为空），客户端将按空口令策略处理
    m_currentClient->setExpectedPasswordDigest(m_passwordSalt, m_passwordDigest);
    qCDebug(lcServer, "为新客户端设置期望摘要：digestLen=%lld, saltLen=%lld", 
            static_cast<long long>(m_passwordDigest.size()),
            static_cast<long long>(m_passwordSalt.size()));
    
    // 连接客户端信号
    connect(m_currentClient, &ClientHandler::connected,
            this, [this]() { onClientConnected(m_currentClient->clientAddress()); }, Qt::QueuedConnection);
    connect(m_currentClient, &ClientHandler::disconnected,
            this, [this]() { onClientDisconnected(m_currentClient->clientAddress()); }, Qt::QueuedConnection);
    connect(m_currentClient, &ClientHandler::authenticated,
            this, [this]() { onClientAuthenticated(m_currentClient->clientAddress()); }, Qt::QueuedConnection);
    connect(m_currentClient, &ClientHandler::messageReceived,
            this, [this](MessageType type, const QByteArray &data) { 
                onMessageReceived(m_currentClient->clientAddress(), type, data); 
            }, Qt::QueuedConnection);
    connect(m_currentClient, &ClientHandler::errorOccurred,
            this, &ServerWorker::onClientError, Qt::QueuedConnection);
}

void ServerWorker::onServerStopped()
{
    qCDebug(lcServer, "TCP服务器已停止");
    
    QMutexLocker locker(&m_serverMutex);
    m_isServerRunning = false;
    m_currentPort = 0;
    
    emit serverStopped();
}

void ServerWorker::onServerError(const QString &error)
{
    qCDebug(lcServer, "TCP服务器错误: %s", error.toUtf8().constData());
    emit serverError(error);
}

void ServerWorker::onClientConnected(const QString &clientAddress)
{
    qCDebug(lcServer, "客户端已连接: %s", clientAddress.toUtf8().constData());
    emit clientConnected(clientAddress);
}

void ServerWorker::onClientDisconnected(const QString &clientAddress)
{
    qCDebug(lcServer, "客户端已断开: %s", clientAddress.toUtf8().constData());
    
    // 清理客户端处理器
    QMutexLocker locker(&m_clientMutex);
    if (m_currentClient) {
        m_currentClient->deleteLater();
        m_currentClient = nullptr;
    }
    // 客户端断开后清空上一帧编码数据，防止下一位客户端误用旧参考帧
    m_prevEncodedFrameData.clear();
    locker.unlock();

    // 客户端断开后停止屏幕捕获
    // 没有客户端连接时停止捕获以节省系统资源
    if (!hasConnectedClients() && m_screenCapture && m_screenCapture->isCapturing()) {
        stopScreenCapture();
        qCDebug(lcServer, "所有客户端已断开，停止屏幕捕获");
    }
    
    emit clientDisconnected(clientAddress);
}

void ServerWorker::onClientAuthenticated(const QString &clientAddress)
{
    qCDebug(lcServer, "客户端已认证: %s", clientAddress.toUtf8().constData());
    
    // 客户端认证成功后启动屏幕捕获
    // 只有在有认证客户端的情况下才开始捕获屏幕，节省系统资源
    if (!m_screenCapture || !m_screenCapture->isCapturing()) {
        startScreenCapture();
        qCDebug(lcServer, "客户端认证成功，启动屏幕捕获");
    }
    
    emit clientAuthenticated(clientAddress);
}

void ServerWorker::onMessageReceived(const QString &clientAddress, MessageType type, const QByteArray &data)
{
    // 转发消息到主线程
    emit messageReceived(clientAddress, type, data);
}

void ServerWorker::onClientError(const QString &error)
{
    qCDebug(lcServer, "客户端错误: %s", error.toUtf8().constData());
}

void ServerWorker::onFrameReady(const QImage &frame)
{
    // 发送屏幕数据给客户端
    sendScreenData(frame);
}

void ServerWorker::onStopTimeout()
{
    qCDebug(lcServer, "服务器停止超时");
    emit serverError(tr("服务器停止超时"));
}

void ServerWorker::cleanupDisconnectedClients()
{
    QMutexLocker locker(&m_clientMutex);
    
    if (m_currentClient && !m_currentClient->isConnected()) {
        qCDebug(lcServer, "清理断开的客户端连接");
        m_currentClient->deleteLater();
        m_currentClient = nullptr;
    }
}

ClientHandler* ServerWorker::findClientHandler(const QString &clientId)
{
    Q_UNUSED(clientId)
    // 单连接模式，直接返回当前客户端
    QMutexLocker locker(&m_clientMutex);
    return m_currentClient;
}

QString ServerWorker::generateClientId(const QString &address, quint16 port)
{
    return QString("%1:%2").arg(address).arg(port);
}

void ServerWorker::sendConnectionRejectionMessage(qintptr socketDescriptor, const QString &errorMessage)
{
    // 创建临时套接字发送拒绝消息
    QTcpSocket tempSocket;
    if (tempSocket.setSocketDescriptor(socketDescriptor)) {
        QByteArray rejectData = errorMessage.toUtf8();
        tempSocket.write(rejectData);
        tempSocket.waitForBytesWritten(3000);
        tempSocket.disconnectFromHost();
        if (tempSocket.state() != QAbstractSocket::UnconnectedState) {
            tempSocket.waitForDisconnected(3000);
        }
    }
}

void ServerWorker::sendProcessedImageData(const DataRecord& record)
{
    if (!m_currentClient) {
        qCDebug(lcServer, "No client connected, skipping processed data send");
        return;
    }

    try {
        // 处理后的数据已经是ARGB32格式的原始像素数据
        // 需要重新构造为可传输的图像格式
        
        // 从处理后的数据创建QImage
        if (record.size.isEmpty() || record.payload.isEmpty()) {
            qCWarning(lcServer, "处理后的数据记录无效");
            return;
        }
        
        // 创建QImage从ARGB32原始数据
        QImage processedImage(reinterpret_cast<const uchar*>(record.payload.constData()),
                             record.size.width(), record.size.height(),
                             QImage::Format_ARGB32);
        
        if (processedImage.isNull()) {
            qCWarning(lcServer, "无法从处理后的数据创建图像");
            return;
        }
        
        // 使用现有的压缩流程处理清洗后的图像
        AdvancedCompressionManager::CompressionStrategy strategy = m_acm ? m_acm->compressionStrategy() : AdvancedCompressionManager::AdaptiveStrategy;
        Compression::ImageFormat format = m_acm ? m_acm->selectOptimalImageFormat(processedImage, strategy)
                                               : Compression::selectOptimalFormat(processedImage);
        int quality = m_acm ? m_acm->selectOptimalQuality(processedImage, format, strategy)
                            : Compression::selectOptimalQuality(processedImage, format);

        // 编码处理后的图像
        QByteArray encodedData = Compression::compressImage(processedImage, format, quality);
        if (encodedData.isEmpty()) {
            qCWarning(lcServer, "处理后图像编码失败");
            return;
        }

        // 构造并发送ScreenData
        ScreenData screenData;
        screenData.x = 0;
        screenData.y = 0;
        screenData.width = static_cast<quint16>(processedImage.width());
        screenData.height = static_cast<quint16>(processedImage.height());
        screenData.imageType = static_cast<quint8>(format);
        screenData.compressionType = 2; // 标记为处理后数据
        screenData.dataSize = static_cast<quint32>(encodedData.size());
        screenData.imageData = encodedData;

        // 发送屏幕数据消息给客户端
        sendMessageToClient(MessageType::SCREEN_DATA, screenData);

        qCDebug(lcServer, "处理后屏幕数据已发送: %dx%d, fmt=%s, quality=%d, 校验和=%llu, 大小=%lld bytes",
                processedImage.width(), processedImage.height(),
                Compression::imageFormatToString(format).toUtf8().constData(), quality,
                record.checksum, static_cast<long long>(encodedData.size()));
                
        // 【新增】存储关键帧到存储管理器
        if (m_storageManager && screenData.compressionType == 2) {
            // 判断是否为关键帧（每10秒存储一次）
            static QDateTime lastKeyFrameTime;
            QDateTime currentTime = QDateTime::currentDateTime();
            bool isKeyFrame = lastKeyFrameTime.isNull() || 
                             lastKeyFrameTime.secsTo(currentTime) >= 10;
            
            if (isKeyFrame) {
                if (m_storageManager->storeFrame(record, true)) {
                    lastKeyFrameTime = currentTime;
                    qCDebug(lcServer, "关键帧已存储: %s", record.id.toUtf8().constData());
                } else {
                    qCWarning(lcServer, "关键帧存储失败: %s", record.id.toUtf8().constData());
                }
            }
        }
                
    } catch (const std::exception &e) {
        qCWarning(lcServer, "发送处理后数据时发生异常: %s", e.what());
    } catch (...) {
        qCWarning(lcServer, "发送处理后数据时发生未知异常");
    }
}