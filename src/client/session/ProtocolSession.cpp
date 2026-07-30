#include "ProtocolSession.h"

#include "client/network/ConnectionManager.h"
#include "client/session/DecodePipeline.h"
#include "common/error/RdError.h"
#include "common/logging/LoggingCategories.h"

ProtocolSession::ProtocolSession(ConnectionManager* connectionManager,
                                 DecodePipeline* pipeline,
                                 QObject* parent)
    : QObject(parent)
    , m_connectionManager(connectionManager)
    , m_pipeline(pipeline) {
    if (m_connectionManager) {
        setupConnections();
    }
}

void ProtocolSession::setupConnections() {
    connect(m_connectionManager, &ConnectionManager::connectionStateChanged,
        this, &ProtocolSession::connectionStateChanged);
    connect(m_connectionManager, &ConnectionManager::errorOccurred,
        this, &ProtocolSession::sessionError);
    connect(m_connectionManager, &ConnectionManager::messageReceived,
        this, &ProtocolSession::onMessageReceived);
}

void ProtocolSession::startSession() {
    if (!m_connectionManager || !m_connectionManager->isAuthenticated()) {
        qCWarning(lcClientSessionProtocol) << "ProtocolSession::startSession() — not authenticated";
        emit sessionError(RdError(ErrorCode::SessionNotAuthenticated,
            tr("无法启动会话 - 未认证"), "ProtocolSession"));
        return;
    }
    if (!m_pipeline) {
        qCWarning(lcClientSessionProtocol) << "ProtocolSession::startSession() — pipeline is null";
        emit sessionError(RdError(ErrorCode::SessionNotAuthenticated,
            tr("解码管线未初始化"), "ProtocolSession"));
        return;
    }
    m_pipeline->start();
}

bool ProtocolSession::isActive() const {
    // 无独立会话标志：认证态 + 管线运行态即为激活。
    // close() 流程中管线先停 → isActive 立即为 false，输入事件停止序列化
    return m_connectionManager && m_connectionManager->isAuthenticated()
        && m_pipeline && m_pipeline->isRunning();
}

bool ProtocolSession::isConnected() const {
    return m_connectionManager && m_connectionManager->isConnected();
}

// ── 消息路由 ──

void ProtocolSession::onMessageReceived(MessageType type, const QByteArray& data) {
    switch (type) {
        case MessageType::SCREEN_DATA:
            if (m_connectionManager && m_connectionManager->isConnected()) {
                handleScreenData(data);
            }
            break;
        case MessageType::CURSOR_SHAPE:
            handleCursorPosition(data);
            break;
        case MessageType::CLIPBOARD_DATA:
            handleClipboardData(data);
            break;
        default:
            break;
    }
}

void ProtocolSession::handleScreenData(const QByteArray& data) {
    // 1. 协议级解码 + 校验
    ScreenData screenData{};
    if (!screenData.decode(data)) {
        qCWarning(lcClientSessionProtocol) << "ProtocolSession::handleScreenData() — decode failed";
        return;
    }
    if (screenData.imageData.isEmpty() || screenData.dataSize == 0) {
        return;
    }
    if (static_cast<quint32>(screenData.imageData.size()) != screenData.dataSize) {
        qCWarning(lcClientSessionProtocol) << "ProtocolSession::handleScreenData() — size mismatch";
        return;
    }

    // 2. 更新远程屏幕尺寸
    if (screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED)) {
        if (screenData.originalWidth > 0 && screenData.originalHeight > 0) {
            QSize newSz(screenData.originalWidth, screenData.originalHeight);
            if (newSz != m_remoteScreenSize) {
                m_remoteScreenSize = newSz;
                emit remoteScreenSizeChanged(m_remoteScreenSize);
            }
        }
    } else if (screenData.width > 0 && screenData.height > 0) {
        // 非缩放帧：直接用帧尺寸（首帧即设置，不等 SCALED 帧）
        QSize newSz(screenData.width, screenData.height);
        if (newSz != m_remoteScreenSize) {
            m_remoteScreenSize = newSz;
            emit remoteScreenSizeChanged(m_remoteScreenSize);
        }
    }

    // 3. 投递到解码管线（同线程直接调用）
    if (m_pipeline && m_pipeline->isRunning()) {
        m_pipeline->enqueueFrame(std::move(screenData), m_remoteScreenSize);
    }
}

void ProtocolSession::handleCursorPosition(const QByteArray& data) {
    CursorMessage message;
    if (message.decode(data)) {
        static int s_cursorRcvCount = 0;
        ++s_cursorRcvCount;
        if (s_cursorRcvCount <= 20 || s_cursorRcvCount % 60 == 0)
            qCDebug(lcClientSessionProtocol) << "[CURSOR-TRACE] CLIENT recv #" << s_cursorRcvCount
                << "pos:" << message.posX << "," << message.posY
                << "size:" << message.width << "x" << message.height;
        emit cursorUpdated(message);
    }
}

void ProtocolSession::handleClipboardData(const QByteArray& data) {
    // 载荷大小守卫：与服务端对称（ClientHandlerWorker 同用 10MB），拒绝超大剪贴板避免内存浪费
    constexpr int kMaxClipboardPayload = 10 * 1024 * 1024;  // 10MB
    if (data.size() > kMaxClipboardPayload) {
        qCWarning(lcClientSessionProtocol) << "剪贴板消息载荷过大，已拒绝，大小:" << data.size();
        return;
    }
    ClipboardMessage message;
    if (!message.decode(data)) {
        qCWarning(lcClientSessionProtocol) << "ProtocolSession::handleClipboardData() — decode failed";
        return;
    }
    if (message.isText()) {
        emit clipboardTextReceived(message.text());
    } else if (message.isImage()) {
        emit clipboardImageReceived(message.imageData());
    }
}

// ── 输入事件序列化 ──

void ProtocolSession::sendMouseEvent(int x, int y, int eventType) {
    if (!isActive()) return;

    MouseEvent mouseEvent;
    mouseEvent.x = x;
    mouseEvent.y = y;
    mouseEvent.eventType = static_cast<MouseEventType>(eventType);
    mouseEvent.wheelDelta = 0;
    m_connectionManager->sendMessage(MessageType::MOUSE_EVENT, mouseEvent);
}

void ProtocolSession::sendKeyboardEvent(int key, int modifiers, bool pressed, const QString& text) {
    if (!isActive()) return;

    KeyboardEvent keyEvent;
    keyEvent.keyCode = key;
    keyEvent.modifiers = modifiers;
    keyEvent.eventType = pressed ? KeyboardEventType::KEY_PRESS : KeyboardEventType::KEY_RELEASE;
    keyEvent.text = text;
    m_connectionManager->sendMessage(MessageType::KEYBOARD_EVENT, keyEvent);
}

void ProtocolSession::sendWheelEvent(int x, int y, int delta, int orientation) {
    Q_UNUSED(orientation);
    if (!isActive()) return;

    MouseEvent wheelEvent;
    wheelEvent.x = x;
    wheelEvent.y = y;
    wheelEvent.eventType = delta > 0 ? MouseEventType::WHEEL_UP : MouseEventType::WHEEL_DOWN;
    wheelEvent.wheelDelta = delta;
    m_connectionManager->sendMessage(MessageType::MOUSE_EVENT, wheelEvent);
}

// ── 剪贴板序列化 ──

void ProtocolSession::sendClipboardText(const QString& text) {
    if (!m_connectionManager || !isConnected()) return;
    ClipboardMessage message(text);
    m_connectionManager->sendMessage(MessageType::CLIPBOARD_DATA, message);
}

void ProtocolSession::sendClipboardImage(const QByteArray& imageData, quint32 width, quint32 height) {
    if (!m_connectionManager || !isConnected()) return;
    ClipboardMessage message(imageData, width, height);
    m_connectionManager->sendMessage(MessageType::CLIPBOARD_DATA, message);
}

// ── 连接控制 ──

void ProtocolSession::disconnectFromHost() {
    if (m_connectionManager) {
        m_connectionManager->disconnectFromHost();
    }
}
