#include "SessionManager.h"
#include "DecodeWorker.h"
#include "../network/ConnectionManager.h"
#include "../../common/core/logging/LoggingCategories.h"
#include "../../common/core/network/Protocol.h"
#ifndef QT_NO_OPENGL
#include "../window/GLTextureViewport.h"
#endif
#include <QtCore/QThread>
#include <QtCore/QTimer>

SessionManager::SessionManager(const QString& connectionId, QObject* parent)
    : QObject(parent)
    , m_connectionId(connectionId)
    , m_connectionManager(new ConnectionManager(this))
    , m_statsTimer(new QTimer(this)) {

    // SessionManager 拥有并管理 ConnectionManager
    setupConnections();

    // 设置性能统计定时器
    m_statsTimer->setInterval(UIConstants::STATS_UPDATE_INTERVAL);
    connect(m_statsTimer, &QTimer::timeout, this, &SessionManager::updatePerformanceStats);

    // 初始化统计数据
    resetStats();
}

SessionManager::~SessionManager() {
    destroyDecodePipeline();
    terminateSession();

    // ConnectionManager 由 Qt 父对象机制自动删除
}

QString SessionManager::connectionId() const {
    return m_connectionId;
}

void SessionManager::startSession() {
    if ( !m_connectionManager || !m_connectionManager->isAuthenticated() ) {
        qCWarning(lcClient) << "SessionManager::startSession() - Cannot start session, not authenticated";
        emit sessionError(tr("无法启动会话 - 未认证"));
        return;
    }

    // 重置统计数据
    resetStats();
    m_stats.sessionStartTime = QDateTime::currentDateTime();

    // 认证成功后创建解码管线
    createDecodePipeline();

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
    // 先停止解码管线（消费者先停）
    destroyDecodePipeline();

    m_statsTimer->stop();

    // 注意：会话终止不发送断开请求，避免重复发送
    // 断开请求统一由 ConnectionManager/TcpClient 在 disconnectFromHost() 时发送
    // 此处只负责清理会话本地数据和状态

    // 清理会话数据
    m_remoteScreenSize = QSize();
    m_lastFpsTime = {};
    m_smoothedFrameDuration = 0.0;
}

bool SessionManager::isActive() const {
    return m_connectionManager && m_connectionManager->isAuthenticated() && m_statsTimer->isActive();
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

    keyEvent.text = text;

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
    m_lastFpsTime = {};
    m_smoothedFrameDuration = 0.0;
}

QString SessionManager::getFormattedPerformanceInfo() const {
    QStringList info;
    info << QString("FPS: %1").arg(m_stats.currentFPS, 0, 'f', 1);

    if ( m_stats.sessionStartTime.isValid() ) {
        qint64 sessionDuration = m_stats.sessionStartTime.secsTo(QDateTime::currentDateTime());
        info << QString("Duration: %1s").arg(sessionDuration);
    }

    return info.join(" | ");
}

void SessionManager::onMessageReceived(MessageType type, const QByteArray& data) {
    switch ( type ) {
        case MessageType::SCREEN_DATA:
            // Fix 3: 仅在连接处于已连接状态时才处理屏幕数据。
            // ConnectionManager::isConnected() 已改为基于 socket 实际状态，
            // TCP 半关闭（ClosingState）期间会正确返回 false。
            if ( m_connectionManager && m_connectionManager->isConnected() ) {
                // 处理屏幕数据
                handleScreenData(data);
            }
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
            qCDebug(lcClient) << "SessionManager::onMessageReceived() - Unhandled message type:" << static_cast<quint32>(type);
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
    // EMA 指数滑动平均：无 QQueue 迭代器开销，无 QDateTime 构造开销。
    // 公式: smoothed = α × instantFps + (1-α) × smoothed
    if ( m_smoothedFrameDuration > 0.0 ) {
        m_stats.currentFPS = 1.0 / m_smoothedFrameDuration;
    } else {
        m_stats.currentFPS = 0.0;
    }
}

void SessionManager::handleScreenData(const QByteArray& data) {
    // 1. 协议级解码 + 校验
    ScreenData screenData{};
    if (!screenData.decode(data)) {
        qCWarning(lcClient) << "SessionManager::handleScreenData() - Failed to decode ScreenData, size:" << data.size();
        return;
    }

    if (screenData.imageData.isEmpty() || screenData.dataSize == 0) {
        qCWarning(lcClient) << "SessionManager::handleScreenData() - ScreenData contains empty image data";
        return;
    }

    if (static_cast<quint32>(screenData.imageData.size()) != screenData.dataSize) {
        qCWarning(lcClient) << "SessionManager::handleScreenData() - Size mismatch, expected:"
                            << screenData.dataSize << "actual:" << screenData.imageData.size();
        return;
    }

    // 2. 通用图像数据校验：非移动区域帧必须至少 2 字节
    const bool isMoveRect = screenData.flags &
        static_cast<quint8>(ScreenDataFlags::MOVE_RECT);
    if (!isMoveRect && screenData.imageData.size() < 2) {
        qCWarning(lcClient) << "SessionManager::handleScreenData() - Image data too small:"
                           << screenData.imageData.size();
        return;
    }

    // 3. 更新 remoteScreenSize（缩放场景）
    if (screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED)) {
        if (screenData.originalWidth > 0 && screenData.originalHeight > 0) {
            m_remoteScreenSize = QSize(screenData.originalWidth, screenData.originalHeight);
        }
    }

    // 全帧 → 更新 remoteScreenSize
    if (screenData.flags & static_cast<quint8>(ScreenDataFlags::FULL_FRAME)) {
        m_remoteScreenSize = QSize(screenData.originalWidth, screenData.originalHeight);
    }
    // Note: 非缩放场景的尺寸更新移到了 DecodeWorker::processOneFrame() 中

    // 4. 投递到解码线程（使用移动语义避免 imageData 深拷贝）
    if (m_decodeWorker && m_decodeWorker->isRunning()) {
        // 在移动前捕获时间戳，用于端到端延迟诊断
        const quint64 captureTs = screenData.captureTimestamp;
        if (m_decodeWorker->enqueueFrame(std::move(screenData), m_remoteScreenSize)) {
            // 5. FPS 统计（基于入队时间）
            const auto now = std::chrono::steady_clock::now();
            if (m_lastFpsTime.time_since_epoch().count() != 0) {
                const double instant = std::chrono::duration<double>(now - m_lastFpsTime).count();
                if (m_smoothedFrameDuration == 0.0) {
                    m_smoothedFrameDuration = instant;
                } else {
                    m_smoothedFrameDuration = kFpsAlpha * instant + (1.0 - kFpsAlpha) * m_smoothedFrameDuration;
                }
            }
            m_lastFpsTime = now;
            m_stats.frameCount++;
            calculateFPS();

            // 诊断：每 120 帧输出一次捕获→接收延迟
            if (captureTs > 0) {
                static int s_diagCount = 0;
                if (++s_diagCount % 120 == 1) {
                    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    qCInfo(lcClient) << "Capture-to-receive latency:" << (nowMs - static_cast<qint64>(captureTs))
                                    << "ms (frame" << m_stats.frameCount << ")";
                }
            }
        } else {
            // 队列满（背压）或队列已停止
            qCDebug(lcClient) << "SessionManager::handleScreenData() - Frame dropped (queue full or stopped)";
        }
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

void SessionManager::resetConnection() {
    // 1) 清理帧时间队列
    m_lastFpsTime = {};
    m_smoothedFrameDuration = 0.0;

    // 2) 重置远程屏幕尺寸
    m_remoteScreenSize = QSize();

    // 3) 重置性能统计（包含 FPS、帧计数等）
    resetStats();

    // 4) 通知外部（UI、RenderManager 等）连接已重置
    emit connectionReset();

    qCInfo(lcClient) << "SessionManager::resetConnection() - Connection state reset complete";
}

// ==================== 剪贴板同步实现 ====================

void SessionManager::sendClipboardText(const QString& text) {
    if ( !m_connectionManager || !isConnected() ) {
        return;
    }

    ClipboardMessage message(text);
    m_connectionManager->sendMessage(MessageType::CLIPBOARD_DATA, message);
    
    qCDebug(lcClient) << "SessionManager::sendClipboardText() - Sent clipboard text, length:" << text.length();
}

void SessionManager::sendClipboardImage(const QByteArray& imageData, quint32 width, quint32 height) {
    if ( !m_connectionManager || !isConnected() ) {
        return;
    }

    ClipboardMessage message(imageData, width, height);
    m_connectionManager->sendMessage(MessageType::CLIPBOARD_DATA, message);
    
    qCDebug(lcClient) << "SessionManager::sendClipboardImage() - Sent clipboard image, size:" << width << "x" << height << "data size:" << imageData.size();
}

void SessionManager::handleClipboardData(const QByteArray& data) {
    ClipboardMessage message;
    if ( !message.decode(data) ) {
        qCWarning(lcClient) << "SessionManager::handleClipboardData() - Failed to decode clipboard message";
        return;
    }

    if (message.isText()) {
        qCDebug(lcClient) << "SessionManager::handleClipboardData() - Received clipboard text, length:" << message.text().length();
        emit clipboardTextReceived(message.text());
    } else if (message.isImage()) {
        qCDebug(lcClient) << "SessionManager::handleClipboardData() - Received clipboard image, size:" << message.width << "x" << message.height << "data size:" << message.imageData().size();
        emit clipboardImageReceived(message.imageData());
    }
}

void SessionManager::createDecodePipeline() {
    if (m_decodeWorker) {
        qCWarning(lcClient) << "SessionManager::createDecodePipeline() - DecodeWorker already exists";
        return;
    }

    QThread* decodeThread = new QThread();
    decodeThread->setObjectName(QString("DecodeThread-%1").arg(m_connectionId));
    decodeThread->start();

    m_decodeWorker = new DecodeWorker(nullptr);
    m_decodeWorker->moveToThread(decodeThread);

#ifndef QT_NO_OPENGL
    if (m_glViewport) {
        m_decodeWorker->setOutputBuffer(m_glViewport->inputBuffer());
        connect(m_decodeWorker, &DecodeWorker::frameDecoded,
                m_glViewport, &GLTextureViewport::doPreRender,
                Qt::QueuedConnection);
    }
#endif

    connect(m_decodeWorker, &DecodeWorker::stopped, this, [this]() {
        qCInfo(lcClient) << "SessionManager: DecodeWorker stopped for" << m_connectionId;
    });

    connect(m_decodeWorker, &DecodeWorker::decodeError, this, [this](const QString& msg) {
        qCWarning(lcClient) << "SessionManager: Decode error for" << m_connectionId << ":" << msg;
    });

    // 注意：不能调用 decodeThread->setParent(m_decodeWorker)，
    // 因为 decodeThread (QThread) 活在当前线程 (SessionThread)，
    // 而 m_decodeWorker 已被 moveToThread(decodeThread) 迁到 DecodeThread。
    // 跨线程 setParent 会触发 Qt 警告，且析构时可能 crash。
    // 线程生命周期由 destroyDecodePipeline() 显式管理：quit → wait → delete worker。

    QMetaObject::invokeMethod(m_decodeWorker, "start", Qt::QueuedConnection);

    qCInfo(lcClient) << "SessionManager: DecodePipeline created for" << m_connectionId
                      << "on thread" << decodeThread->objectName();
}

void SessionManager::destroyDecodePipeline() {
    if (!m_decodeWorker) return;

    qCInfo(lcClient) << "SessionManager::destroyDecodePipeline() - Stopping decode pipeline for"
                     << m_connectionId;

#ifndef QT_NO_OPENGL
    disconnect(m_decodeWorker, &DecodeWorker::frameDecoded,
               m_glViewport, &GLTextureViewport::doPreRender);
#endif

    m_decodeWorker->requestStop();

    QThread* decodeThread = m_decodeWorker->thread();
    if (decodeThread && decodeThread->isRunning()) {
        decodeThread->quit();
        if (!decodeThread->wait(3000)) {
            qCWarning(lcClient) << "SessionManager::destroyDecodePipeline() - DecodeThread quit timeout, forcing";
            decodeThread->terminate();
            decodeThread->wait(1000);
        }
    }

    delete m_decodeWorker;
    m_decodeWorker = nullptr;

    // 由于不再通过 setParent 将 decodeThread 设为 m_decodeWorker 的子对象，
    // 需要手动清理 QThread。quit()+wait() 已确保事件循环退出。
    if (decodeThread) {
        delete decodeThread;
    }

    qCInfo(lcClient) << "SessionManager::destroyDecodePipeline() - Decode pipeline destroyed for"
                     << m_connectionId;
}

