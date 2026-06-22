#include "ProtocolSession.h"
#include "DecodePipeline.h"
#include "../network/ConnectionManager.h"
#include "../../common/core/logging/LoggingCategories.h"

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

ProtocolSession::~ProtocolSession() {
    terminateSession();
}

QString ProtocolSession::connectionId() const {
    return m_connectionId;
}

void ProtocolSession::setupConnections() {
    connect(m_connectionManager, &ConnectionManager::connectionStateChanged,
        this, &ProtocolSession::connectionStateChanged);
    connect(m_connectionManager, &ConnectionManager::messageReceived,
        this, &ProtocolSession::onMessageReceived);
}

void ProtocolSession::startSession() {
    if (!m_connectionManager || !m_connectionManager->isAuthenticated()) {
        qCWarning(lcSession) << "ProtocolSession::startSession() — not authenticated";
        emit sessionError(RdError(ErrorCode::SessionNotAuthenticated,
            tr("无法启动会话 - 未认证"), "ProtocolSession"));
        return;
    }
    if (!m_pipeline) {
        qCWarning(lcSession) << "ProtocolSession::startSession() — pipeline is null";
        emit sessionError(RdError(ErrorCode::SessionNotAuthenticated,
            tr("解码管线未初始化"), "ProtocolSession"));
        return;
    }
    m_sessionActive = true;
    m_pipeline->start();
}

void ProtocolSession::suspendSession() {
    // 暂停 pipeline（保持 worker 存活但停止处理）
}

void ProtocolSession::resumeSession() {
    if (m_connectionManager && m_connectionManager->isAuthenticated()) {
        m_connectionManager->sendMessage(MessageType::HANDSHAKE_REQUEST, BaseMessage());
    }
}

void ProtocolSession::terminateSession() {
    m_sessionActive = false;
    m_remoteScreenSize = QSize();
}

bool ProtocolSession::isActive() const {
    return m_sessionActive && m_connectionManager
        && m_connectionManager->isAuthenticated();
}

bool ProtocolSession::isAuthenticated() const {
    return m_connectionManager && m_connectionManager->isAuthenticated();
}

QString ProtocolSession::currentHost() const {
    return m_connectionManager ? m_connectionManager->currentHost() : QString();
}

int ProtocolSession::currentPort() const {
    return m_connectionManager ? m_connectionManager->currentPort() : 0;
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
        case MessageType::CURSOR_POSITION:
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
        qCWarning(lcSession) << "ProtocolSession::handleScreenData() — decode failed";
        return;
    }
    if (screenData.imageData.isEmpty() || screenData.dataSize == 0) {
        return;
    }
    if (static_cast<quint32>(screenData.imageData.size()) != screenData.dataSize) {
        qCWarning(lcSession) << "ProtocolSession::handleScreenData() — size mismatch";
        return;
    }

    // 2. JPEG 头部校验
    if (screenData.imageData.size() >= 2) {
        unsigned char b0 = static_cast<unsigned char>(screenData.imageData[0]);
        unsigned char b1 = static_cast<unsigned char>(screenData.imageData[1]);
        if (b0 != 0xFF || b1 != 0xD8) {
            qCWarning(lcSession) << "ProtocolSession::handleScreenData() — invalid JPEG header";
            return;
        }
    }

    // 3. 更新远程屏幕尺寸（缩放场景）
    if (screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED)) {
        if (screenData.originalWidth > 0 && screenData.originalHeight > 0) {
            m_remoteScreenSize = QSize(screenData.originalWidth, screenData.originalHeight);
        }
    }

    // 4. 投递到解码管线（同线程直接调用）
    if (m_pipeline && m_pipeline->isRunning()) {
        m_pipeline->enqueueFrame(std::move(screenData), m_remoteScreenSize);
    }
}

void ProtocolSession::handleCursorPosition(const QByteArray& data) {
    CursorMessage message;
    if (message.decode(data)) {
        emit remoteCursorTypeUpdated(message.cursorType);
    }
}

void ProtocolSession::handleClipboardData(const QByteArray& data) {
    ClipboardMessage message;
    if (!message.decode(data)) {
        qCWarning(lcSession) << "ProtocolSession::handleClipboardData() — decode failed";
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

void ProtocolSession::connectToHost(const QString& host, int port) {
    if (m_connectionManager) {
        m_connectionManager->connectToHost(host, port);
    }
}

void ProtocolSession::disconnectFromHost() {
    if (m_connectionManager) {
        m_connectionManager->disconnectFromHost();
    }
}

void ProtocolSession::resetConnection() {
    m_remoteScreenSize = QSize();
    m_sessionActive = false;
    emit connectionReset();
    qCInfo(lcSession) << "ProtocolSession::resetConnection() — complete";
}
