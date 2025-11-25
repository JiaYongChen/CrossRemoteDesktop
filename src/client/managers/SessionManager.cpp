#include "SessionManager.h"
#include "../network/ConnectionManager.h"
#include <QtCore/QDebug>
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QMessageLogger>
#include <QtCore/QBuffer>
#include <QtCore/QDataStream>
#include <QtCore/QTimer>
#include <QtCore/QMutexLocker>

SessionManager::SessionManager(const QString& connectionId, QObject* parent)
    : QObject(parent)
    , m_connectionId(connectionId)
    , m_connectionManager(new ConnectionManager(this))
    , m_frameDataMutex(new QMutex())
    , m_screenImageQueueMutex(new QMutex())
    , m_statsTimer(new QTimer(this))
    , m_frameRate(30) {

    // SessionManager 拥有并管理 ConnectionManager
    setupConnections();

    // 设置性能统计定时器
    m_statsTimer->setInterval(UIConstants::STATS_UPDATE_INTERVAL);
    connect(m_statsTimer, &QTimer::timeout, this, &SessionManager::updatePerformanceStats);

    // 初始化统计数据
    resetStats();
}

SessionManager::~SessionManager() {
    terminateSession();

    // ConnectionManager 由 Qt 父对象机制自动删除
    delete m_frameDataMutex;
    delete m_screenImageQueueMutex;
}

QString SessionManager::connectionId() const {
    return m_connectionId;
}

void SessionManager::startSession() {
    if ( !m_connectionManager || !m_connectionManager->isAuthenticated() ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "SessionManager: Cannot start session - not authenticated";
        emit sessionError(tr("无法启动会话 - 未认证"));
        return;
    }

    // 重置统计数据
    resetStats();
    m_stats.sessionStartTime = QDateTime::currentDateTime();

    // 启动统计定时器
    m_statsTimer->start();
}

void SessionManager::suspendSession() {
    m_statsTimer->stop();
}

void SessionManager::resumeSession() {
    m_statsTimer->start();

    // 发送恢复会话的消息 (使用握手请求作为替代)
    if ( m_connectionManager && m_connectionManager->isAuthenticated() ) {
        m_connectionManager->sendMessage(MessageType::HANDSHAKE_REQUEST, BaseMessage());
    }
}

void SessionManager::terminateSession() {
    m_statsTimer->stop();

    // 注意：会话终止不发送断开请求，避免重复发送
    // 断开请求统一由 ConnectionManager/TcpClient 在 disconnectFromHost() 时发送
    // 此处只负责清理会话本地数据和状态

    // 清理会话数据
    m_currentScreen = QPixmap();
    m_remoteScreenSize = QSize();
    m_frameTimes.clear();
}

bool SessionManager::isActive() const {
    return m_connectionManager && m_connectionManager->isAuthenticated() && m_statsTimer->isActive();
}

QPixmap SessionManager::currentScreen() const {
    return m_currentScreen;
}

QSize SessionManager::remoteScreenSize() const {
    return m_remoteScreenSize;
}

void SessionManager::sendMouseEvent(int x, int y, int eventType) {
    if ( !isActive() || !m_connectionManager || !m_connectionManager->isAuthenticated() ) {
        return;
    }

    MouseEvent mouseEvent;
    mouseEvent.x = x;
    mouseEvent.y = y;
    mouseEvent.eventType = static_cast<MouseEventType>(eventType);
    mouseEvent.wheelDelta = 0;

    m_connectionManager->sendMessage(MessageType::MOUSE_EVENT, mouseEvent);
}

void SessionManager::sendKeyboardEvent(int key, int modifiers, bool pressed, const QString& text) {
    if ( !isActive() || !m_connectionManager || !m_connectionManager->isAuthenticated() ) {
        return;
    }

    KeyboardEvent keyEvent;
    keyEvent.keyCode = key;
    keyEvent.modifiers = modifiers;
    keyEvent.eventType = pressed ? KeyboardEventType::KEY_PRESS : KeyboardEventType::KEY_RELEASE;

    // 复制文本，确保不超过缓冲区大小
    QByteArray textBytes = text.toUtf8();
    int copySize = qMin(textBytes.size(), static_cast<int>(sizeof(keyEvent.text) - 1));
    memcpy(keyEvent.text, textBytes.constData(), copySize);
    keyEvent.text[copySize] = '\0';

    m_connectionManager->sendMessage(MessageType::KEYBOARD_EVENT, keyEvent);
}

void SessionManager::sendWheelEvent(int x, int y, int delta, int orientation) {
    if ( !isActive() || !m_connectionManager || !m_connectionManager->isAuthenticated() ) {
        return;
    }

    Q_UNUSED(orientation);

    MouseEvent wheelEvent;
    wheelEvent.x = x;
    wheelEvent.y = y;
    wheelEvent.eventType = delta > 0 ? MouseEventType::WHEEL_UP : MouseEventType::WHEEL_DOWN;
    wheelEvent.wheelDelta = delta;

    m_connectionManager->sendMessage(MessageType::MOUSE_EVENT, wheelEvent);
}

SessionManager::PerformanceStats SessionManager::performanceStats() const {
    return m_stats;
}

void SessionManager::resetStats() {
    m_stats.currentFPS = 0.0;
    m_stats.sessionStartTime = QDateTime();
    m_stats.frameCount = 0;
    m_frameTimes.clear();
}

QString SessionManager::getFormattedPerformanceInfo() const {
    QStringList info;
    info << QString("FPS: %1").arg(m_stats.currentFPS, 0, 'f', 1);
    info << QString("Frame Rate: %1").arg(m_frameRate);

    if ( m_stats.sessionStartTime.isValid() ) {
        qint64 sessionDuration = m_stats.sessionStartTime.secsTo(QDateTime::currentDateTime());
        info << QString("Duration: %1s").arg(sessionDuration);
    }

    return info.join(" | ");
}

void SessionManager::setFrameRate(int fps) {
    m_frameRate = qBound(1, fps, 60);

    if ( isActive() && m_connectionManager && m_connectionManager->isAuthenticated() ) {
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << m_frameRate;
        // ConfigUpdate not available in protocol, using HANDSHAKE_REQUEST
        // m_connectionManager->sendMessage(MessageType::HANDSHAKE_REQUEST, data);
    }
}

int SessionManager::frameRate() const {
    return m_frameRate;
}

void SessionManager::onMessageReceived(MessageType type, const QByteArray& data) {
    switch ( type ) {
        case MessageType::SCREEN_DATA:
            // 处理屏幕数据
            handleScreenData(data);
            break;
        case MessageType::CURSOR_POSITION:
            // 处理光标位置数据
            handleCursorPosition(data);
            break;
        case MessageType::CLIPBOARD_DATA:
            // 处理剪贴板数据（文本或图片）
            handleClipboardData(data);
            break;
        default:
            // 其他消息类型忽略
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient)
                << "SessionManager: Unhandled message type:" << static_cast<quint32>(type);
            break;
    }
}

void SessionManager::updatePerformanceStats() {
    // 只更新FPS相关的统计信息，网络统计信息已移除
    emit performanceStatsUpdated(m_stats);
}

void SessionManager::setupConnections() {
    // 转发连接状态变化信号（用于 UI 更新）
    connect(m_connectionManager, &ConnectionManager::connectionStateChanged,
        this, &SessionManager::connectionStateChanged);

    // m_connectionManager 在构造函数中创建，永远不为空
    connect(m_connectionManager, &ConnectionManager::messageReceived,
        this, &SessionManager::onMessageReceived);
}

void SessionManager::calculateFPS() {
    if ( m_frameTimes.size() < 2 ) {
        m_stats.currentFPS = 0.0;
        return;
    }

    QDateTime oldest = m_frameTimes.first();
    QDateTime newest = m_frameTimes.last();

    qint64 timeSpan = oldest.msecsTo(newest);
    if ( timeSpan > 0 ) {
        m_stats.currentFPS = (m_frameTimes.size() - 1) * 1000.0 / timeSpan;
    } else {
        m_stats.currentFPS = 0.0;
    }
}

void SessionManager::handleScreenData(const QByteArray& data) {
    // 使用正确的ScreenData结构体解码数据
    ScreenData screenData{};
    if ( !screenData.decode(data) ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
            << "Failed to decode ScreenData from received data, size:" << data.size();
        return;
    }

    // 验证数据完整性
    if ( screenData.imageData.isEmpty() || screenData.dataSize == 0 ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
            << "ScreenData contains empty image data";
        return;
    }

    if ( static_cast<quint32>(screenData.imageData.size()) != screenData.dataSize ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
            << "ScreenData size mismatch - expected:" << screenData.dataSize
            << "actual:" << screenData.imageData.size();
        return;
    }

    // 验证JPEG格式头部（JPEG文件以0xFF 0xD8开头）
    if ( screenData.imageData.size() >= 2 ) {
        unsigned char byte0 = static_cast<unsigned char>(screenData.imageData[0]);
        unsigned char byte1 = static_cast<unsigned char>(screenData.imageData[1]);
        if ( byte0 != 0xFF || byte1 != 0xD8 ) {
            qCWarning(lcClient) << "接收到的数据不是有效的JPEG格式，前2字节:"
                << QString("0x%1 0x%2")
                .arg(byte0, 2, 16, QChar('0'))
                .arg(byte1, 2, 16, QChar('0'));
        }
    }

    QByteArray frameData;
    {
        QMutexLocker locker(m_frameDataMutex);
        // 直接使用接收到的JPEG数据
        frameData = screenData.imageData;
        m_previousFrameData = screenData.imageData;
    }

    // 从JPEG格式数据加载QImage
    QImage image;
    bool loaded = image.loadFromData(frameData, "JPEG");

    if ( loaded && !image.isNull() ) {
        // 成功加载图像，转换为QPixmap并更新（仅用于内部缓存）
        m_currentScreen = QPixmap::fromImage(image);
        m_remoteScreenSize = image.size();

        // 更新性能统计
        m_frameTimes.enqueue(QDateTime::currentDateTime());
        if ( m_frameTimes.size() > 100 ) {
            m_frameTimes.dequeue();
        }
        m_stats.frameCount++;
        calculateFPS();

        // 将图片放入队列，替代信号槽机制
        {
            QMutexLocker locker(m_screenImageQueueMutex);
            // 如果队列已满，移除最旧的图片
            while ( m_screenImageQueue.size() >= MAX_QUEUE_SIZE ) {
                m_screenImageQueue.dequeue();
                qCDebug(lcClient) << "SessionManager: Queue full, dropped oldest frame";
            }
            m_screenImageQueue.enqueue(image);
        }

        // 注意：不再发射screenUpdated信号，改用ClientManager定时拉取
        // emit screenUpdated(image);
    } else {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
            << "Failed to load JPEG image from frame data, size:" << frameData.size()
            << "first 16 bytes:" << frameData.left(16).toHex();
    }
}

void SessionManager::handleCursorPosition(const QByteArray& data) {
    // 使用 CursorMessage 解析光标类型数据
    CursorMessage message;
    if ( !message.decode(data) ) {
        qCWarning(lcClient) << "Failed to decode cursor type message";
        return;
    }

    // 仅发射光标类型更新信号
    emit remoteCursorTypeUpdated(message.cursorType);
}

QString SessionManager::currentHost() const {
    return m_connectionManager ? m_connectionManager->currentHost() : QString();
}

int SessionManager::currentPort() const {
    return m_connectionManager ? m_connectionManager->currentPort() : 0;
}

bool SessionManager::isConnected() const {
    return m_connectionManager && m_connectionManager->isConnected();
}

bool SessionManager::isAuthenticated() const {
    return m_connectionManager && m_connectionManager->isAuthenticated();
}

void SessionManager::connectToHost(const QString& host, int port) {
    if ( m_connectionManager ) {
        m_connectionManager->connectToHost(host, port);
    }
}

void SessionManager::disconnectFromHost() {
    if ( m_connectionManager ) {
        m_connectionManager->disconnectFromHost();
    }
}

// ==================== 剪贴板同步实现 ====================

void SessionManager::sendClipboardText(const QString& text) {
    if ( !m_connectionManager || !isConnected() ) {
        return;
    }

    ClipboardMessage message(text);
    m_connectionManager->sendMessage(MessageType::CLIPBOARD_DATA, message);
    
    qDebug() << "SessionManager: Sent clipboard text, length:" << text.length();
}

void SessionManager::sendClipboardImage(const QByteArray& imageData, quint32 width, quint32 height) {
    if ( !m_connectionManager || !isConnected() ) {
        return;
    }

    ClipboardMessage message(imageData, width, height);
    m_connectionManager->sendMessage(MessageType::CLIPBOARD_DATA, message);
    
    qDebug() << "SessionManager: Sent clipboard image, size:" << width << "x" << height
             << "data size:" << imageData.size();
}

void SessionManager::handleClipboardData(const QByteArray& data) {
    ClipboardMessage message;
    if ( !message.decode(data) ) {
        qWarning() << "SessionManager: Failed to decode clipboard message";
        return;
    }

    if (message.isText()) {
        qDebug() << "SessionManager: Received clipboard text, length:" << message.text().length();
        emit clipboardTextReceived(message.text());
    } else if (message.isImage()) {
        qDebug() << "SessionManager: Received clipboard image, size:" << message.width << "x" << message.height
                 << "data size:" << message.imageData().size();
        emit clipboardImageReceived(message.imageData());
    }
}

// ==================== 图片队列操作实现 ====================

bool SessionManager::hasScreenImage() const {
    QMutexLocker locker(m_screenImageQueueMutex);
    return !m_screenImageQueue.isEmpty();
}

QImage SessionManager::dequeueScreenImage() {
    QMutexLocker locker(m_screenImageQueueMutex);
    if ( m_screenImageQueue.isEmpty() ) {
        return QImage();
    }
    QImage image = m_screenImageQueue.dequeue();
    qCDebug(lcClient) << "SessionManager: Image dequeued, remaining:" << m_screenImageQueue.size();
    return image;
}