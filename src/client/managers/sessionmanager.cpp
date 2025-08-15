#include "sessionmanager.h"
#include "connectionmanager.h"
#include "../../client/tcpclient.h"
#include "../core/compression.h"
#include <QDebug>
#include "../../common/core/logging_categories.h"
#include <QMessageLogger>
#include <QBuffer>
#include <QDataStream>

SessionManager::SessionManager(ConnectionManager *connectionManager, QObject *parent)
    : QObject(parent)
    , m_connectionManager(connectionManager)
    , m_tcpClient(nullptr)
    , m_sessionState(Inactive)
    , m_statsTimer(new QTimer(this))
    , m_frameRate(30)
    , m_compressionLevel(5)
{
    if (m_connectionManager) {
        m_tcpClient = m_connectionManager->tcpClient();
        setupConnections();
    }
    
    // 设置性能统计定时器
    m_statsTimer->setInterval(UIConstants::STATS_UPDATE_INTERVAL);
    connect(m_statsTimer, &QTimer::timeout, this, &SessionManager::updatePerformanceStats);
    
    // 初始化统计数据
    resetStats();
}

SessionManager::~SessionManager()
{
    terminateSession();
}

void SessionManager::startSession()
{
    if (m_sessionState != Inactive) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "SessionManager: Session already active or starting";
        return;
    }
    
    if (!m_connectionManager || !m_connectionManager->isAuthenticated()) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "SessionManager: Cannot start session - not authenticated";
        emit sessionError(tr("无法启动会话 - 未认证"));
        return;
    }
    
    setSessionState(Initializing);
    
    // 重置统计数据
    resetStats();
    m_stats.sessionStartTime = QDateTime::currentDateTime();
    
    // 启动统计定时器
    m_statsTimer->start();
    
    // 发送会话启动请求
    if (m_tcpClient) {
        QByteArray sessionData;
        QDataStream stream(&sessionData, QIODevice::WriteOnly);
        stream << m_frameRate << m_compressionLevel;
        m_tcpClient->sendMessage(MessageType::HANDSHAKE_REQUEST, sessionData);
    }
    
    setSessionState(Active);
}

void SessionManager::suspendSession()
{
    if (m_sessionState != Active) {
        return;
    }
    
    setSessionState(Suspended);
    m_statsTimer->stop();
    
    if (m_tcpClient) {
        // SessionSuspend not available in protocol, using DISCONNECT_REQUEST
         m_tcpClient->sendMessage(MessageType::DISCONNECT_REQUEST);
    }
}

void SessionManager::resumeSession()
{
    if (m_sessionState != Suspended) {
        return;
    }
    
    setSessionState(Active);
    m_statsTimer->start();
    
    if (m_tcpClient) {
        // SessionResume not available in protocol, using HANDSHAKE_REQUEST
        m_tcpClient->sendMessage(MessageType::HANDSHAKE_REQUEST);
    }
}

void SessionManager::terminateSession()
{
    if (m_sessionState == Inactive || m_sessionState == Terminated) {
        return;
    }
    
    setSessionState(Terminated);
    m_statsTimer->stop();
    
    if (m_tcpClient) {
        // SessionEnd not available in protocol, using DISCONNECT_REQUEST
         m_tcpClient->sendMessage(MessageType::DISCONNECT_REQUEST);
    }
    
    // 清理会话数据
    m_currentScreen = QPixmap();
    m_remoteScreenSize = QSize();
    m_frameTimes.clear();
    
    setSessionState(Inactive);
}

SessionManager::SessionState SessionManager::sessionState() const
{
    return m_sessionState;
}

bool SessionManager::isActive() const
{
    return m_sessionState == Active;
}

QPixmap SessionManager::currentScreen() const
{
    return m_currentScreen;
}

QSize SessionManager::remoteScreenSize() const
{
    return m_remoteScreenSize;
}

void SessionManager::sendMouseEvent(int x, int y, int buttons, int eventType)
{
    if (!isActive() || !m_tcpClient) {
        return;
    }
    
    m_tcpClient->sendMouseEvent(x, y, buttons, eventType);
}

void SessionManager::sendKeyboardEvent(int key, int modifiers, bool pressed, const QString &text)
{
    if (!isActive() || !m_tcpClient) {
        return;
    }
    
    m_tcpClient->sendKeyboardEvent(key, modifiers, pressed, text);
}

void SessionManager::sendWheelEvent(int x, int y, int delta, int orientation)
{
    if (!isActive() || !m_tcpClient) {
        return;
    }
    
    m_tcpClient->sendWheelEvent(x, y, delta, orientation);
}

SessionManager::PerformanceStats SessionManager::performanceStats() const
{
    return m_stats;
}

void SessionManager::resetStats()
{
    m_stats.currentFPS = 0.0;
    m_stats.sessionStartTime = QDateTime();
    m_stats.frameCount = 0;
    m_frameTimes.clear();
}

void SessionManager::setFrameRate(int fps)
{
    m_frameRate = qBound(1, fps, 60);
    
    if (isActive() && m_tcpClient) {
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << m_frameRate;
        // ConfigUpdate not available in protocol, using HANDSHAKE_REQUEST
        m_tcpClient->sendMessage(MessageType::HANDSHAKE_REQUEST, data);
    }
}

int SessionManager::frameRate() const
{
    return m_frameRate;
}

void SessionManager::setCompressionLevel(int level)
{
    m_compressionLevel = qBound(0, level, 9);
    
    if (isActive() && m_tcpClient) {
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << m_compressionLevel;
        // ConfigUpdate not available in protocol, using HANDSHAKE_REQUEST
        m_tcpClient->sendMessage(MessageType::HANDSHAKE_REQUEST, data);
    }
}

int SessionManager::compressionLevel() const
{
    return m_compressionLevel;
}

void SessionManager::onConnectionStateChanged()
{
    if (!m_connectionManager) {
        return;
    }
    
    auto state = m_connectionManager->connectionState();
    
    if (state == ConnectionManager::Disconnected || state == ConnectionManager::Error) {
        terminateSession();
    } else if (state == ConnectionManager::Authenticated && m_sessionState == Inactive) {
        // 连接认证成功后可以启动会话
        // 这里不自动启动，等待外部调用startSession()
    }
}

void SessionManager::onScreenDataReceived(const QPixmap &pixmap)
{
    if (!isActive()) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "SessionManager::onScreenDataReceived - Session not active, ignoring";
        return;
    }
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "SessionManager::onScreenDataReceived - Pixmap size:" << pixmap.size() 
             << "isNull:" << pixmap.isNull();
    
    m_currentScreen = pixmap;
    m_remoteScreenSize = pixmap.size();
    
    // 记录帧时间用于FPS计算
    QDateTime now = QDateTime::currentDateTime();
    m_frameTimes.enqueue(now);
    
    // 限制帧时间历史记录数量
    while (m_frameTimes.size() > UIConstants::MAX_FRAME_HISTORY) {
        m_frameTimes.dequeue();
    }
    
    m_stats.frameCount++;
    calculateFPS();
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "SessionManager::onScreenDataReceived - Emitting screenUpdated signal";
    emit screenUpdated(pixmap);
}

void SessionManager::onMessageReceived(MessageType type, const QByteArray &data)
{
    switch (type) {
    case MessageType::SCREEN_DATA:
        processScreenData(data);
        break;
    case MessageType::HANDSHAKE_RESPONSE:
        // InputResponse not available, using HANDSHAKE_RESPONSE
        processInputResponse(data);
        break;
    case MessageType::DISCONNECT_REQUEST:
         // SessionEnd not available, using DISCONNECT_REQUEST
         terminateSession();
        break;
    default:
        // 其他消息类型由其他组件处理
        break;
    }
}

void SessionManager::updatePerformanceStats()
{
    // 只更新FPS相关的统计信息，网络统计信息已移除
    emit performanceStatsUpdated(m_stats);
}

void SessionManager::setSessionState(SessionState state)
{
    if (m_sessionState != state) {
        m_sessionState = state;
        emit sessionStateChanged(state);
    }
}

void SessionManager::setupConnections()
{
    if (m_connectionManager) {
        connect(m_connectionManager, &ConnectionManager::connectionStateChanged,
                this, &SessionManager::onConnectionStateChanged);
    }
    
    if (m_tcpClient) {
        connect(m_tcpClient, &TcpClient::screenDataReceived,
                this, &SessionManager::onScreenDataReceived);
        connect(m_tcpClient, &TcpClient::messageReceived,
                this, &SessionManager::onMessageReceived);
    }
}

void SessionManager::processScreenData(const QByteArray &data)
{
    if (data.isEmpty()) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "SessionManager: Received empty screen data";
        return;
    }
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "SessionManager: Processing screen data, size:" << data.size();
    
    // 尝试自动检测压缩算法并解压缩
    QByteArray decompressed = CompressionUtils::autoDecompress(data);
    
    if (decompressed.isEmpty()) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "SessionManager: Failed to decompress screen data, trying fallback methods";
        
        // 尝试直接使用数据（可能未压缩）
        QPixmap pixmap;
        if (pixmap.loadFromData(data)) {
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "SessionManager: Successfully loaded uncompressed screen data";
            onScreenDataReceived(pixmap);
            return;
        }
        
        // 尝试不同的压缩算法
        QList<CompressionUtils::Algorithm> algorithms = {
            CompressionUtils::Zlib,
            CompressionUtils::LZ4,
            CompressionUtils::Zstd
        };
        
        for (auto algorithm : algorithms) {
            QByteArray testDecompressed = CompressionUtils::decompress(data, algorithm);
            if (!testDecompressed.isEmpty()) {
                QPixmap pixmap;
                if (pixmap.loadFromData(testDecompressed)) {
                    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "SessionManager: Successfully decompressed with algorithm:" << (int)algorithm;
                    onScreenDataReceived(pixmap);
                    return;
                }
            }
        }
        
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "SessionManager: All decompression attempts failed";
        return;
    }
    
    // 将解压缩的数据转换为QPixmap
    QPixmap pixmap;
    if (pixmap.loadFromData(decompressed)) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "SessionManager: Successfully loaded screen data, decompressed size:" << decompressed.size();
        onScreenDataReceived(pixmap);
    } else {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "SessionManager: Failed to load screen data from decompressed bytes, size:" << decompressed.size();
    }
}

void SessionManager::processInputResponse(const QByteArray &data)
{
    // 处理输入响应（如果需要）
    Q_UNUSED(data)
}

void SessionManager::calculateFPS()
{
    if (m_frameTimes.size() < 2) {
        m_stats.currentFPS = 0.0;
        return;
    }
    
    QDateTime oldest = m_frameTimes.first();
    QDateTime newest = m_frameTimes.last();
    
    qint64 timeSpan = oldest.msecsTo(newest);
    if (timeSpan > 0) {
        m_stats.currentFPS = (m_frameTimes.size() - 1) * 1000.0 / timeSpan;
    } else {
        m_stats.currentFPS = 0.0;
    }
}