#include "clienthandler.h"
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTimer>
#include "inputsimulator.h"
#include "../common/core/protocol.h"
#include "../common/core/encryption.h"
#include "../common/core/networkconstants.h"
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include "../common/core/logging_categories.h"
#include <QtCore/QDataStream>
#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QMessageLogger>
#include <cstring>

// ClientHandler implementation
ClientHandler::ClientHandler(qintptr socketDescriptor, QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_clientPort(0)
    , m_isAuthenticated(false)
    , m_failedAuthCount(0)
    , m_connectionTime(QDateTime::currentDateTime())
    , m_lastHeartbeat(QDateTime::currentDateTime())
    , m_heartbeatTimer(new QTimer(this))
    , m_heartbeatCheckTimer(new QTimer(this))
    , m_bytesReceived(0)
    , m_bytesSent(0)
    , m_inputSimulator(new InputSimulator(this))
{
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcServerManager) << "ClientHandler constructor socketDescriptor:" << socketDescriptor;
    if (!m_socket->setSocketDescriptor(socketDescriptor)) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Failed to set socket descriptor, error:" << m_socket->errorString();
        return;
    }
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcServerManager) << "Socket descriptor set successfully";
    
    // 设置TCP优化选项
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);  // TCP_NODELAY
    m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 256 * 1024);  // 256KB发送缓冲区
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 256 * 1024);  // 256KB接收缓冲区
    
    m_clientAddress = m_socket->peerAddress().toString();
    m_clientPort = m_socket->peerPort();
    m_clientId = QString("%1:%2").arg(m_clientAddress).arg(m_clientPort);
    
    // 连接信号
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientHandler::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientHandler::onDisconnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &ClientHandler::onError);
    
    // 设置心跳检查定时器（只检查，不主动发送）
    m_heartbeatCheckTimer->setInterval(NetworkConstants::HEARTBEAT_TIMEOUT);
    connect(m_heartbeatCheckTimer, &QTimer::timeout, this, &ClientHandler::checkHeartbeat);
    
    // 初始化输入模拟器
    if (!m_inputSimulator->initialize()) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Failed to initialize input simulator for client:" << m_clientId;
    }
    
    // 启动心跳检查定时器
    m_heartbeatCheckTimer->start();
    
    // 延迟发射connected信号，确保信号连接已建立
    QTimer::singleShot(0, this, [this]() {
        emit connected();
    });
}

ClientHandler::~ClientHandler()
{
    if (m_socket) {
        m_socket->disconnectFromHost();
    }
}

QString ClientHandler::clientAddress() const
{
    return m_clientAddress;
}

quint16 ClientHandler::clientPort() const
{
    return m_clientPort;
}

QString ClientHandler::clientId() const
{
    return m_clientId;
}

bool ClientHandler::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

bool ClientHandler::isAuthenticated() const
{
    return m_isAuthenticated;
}

void ClientHandler::sendMessage(MessageType type, const IMessageCodec &message)
{
    if (!isConnected()) {
        return;
    }
    
    // 使用编解码器创建消息
    QByteArray messageData = Protocol::createMessage(type, message);
    
    qint64 bytesWritten = m_socket->write(messageData);
    if (bytesWritten > 0) {
        m_bytesSent += bytesWritten;
    }
}

void ClientHandler::disconnectClient()
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        // 停止心跳定时器
        m_heartbeatTimer->stop();
        m_heartbeatCheckTimer->stop();
        
        // 优雅地断开连接
        m_socket->disconnectFromHost();
        
        // 如果在指定时间内没有断开，强制关闭
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            if (!m_socket->waitForDisconnected(3000)) {
                m_socket->abort();
            }
        }
    }
}

void ClientHandler::forceDisconnect()
{
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServerManager) << "Force disconnecting client:" << clientAddress();
    
    if (m_socket) {
        // 停止心跳定时器
        if (m_heartbeatTimer) {
            m_heartbeatTimer->stop();
        }
        if (m_heartbeatCheckTimer) {
            m_heartbeatCheckTimer->stop();
        }
        
        // 清理接收缓冲区
        m_receiveBuffer.clear();
        
        // 断开所有信号连接，防止在断开过程中触发其他事件
        m_socket->disconnect();
        
        // 立即强制断开连接，不等待
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->abort();
        }
        
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServerManager) << "Client forcefully disconnected:" << clientAddress();
    }
}

quint64 ClientHandler::bytesReceived() const
{
    return m_bytesReceived;
}

quint64 ClientHandler::bytesSent() const
{
    return m_bytesSent;
}

QDateTime ClientHandler::connectionTime() const
{
    return m_connectionTime;
}

void ClientHandler::onReadyRead()
{
    // 说明：
    // - 支持粘包/半包：循环解析，若数据不足一个完整帧则等待更多数据。
    // - 解析与消费：使用 Protocol::parseMessage 解析当前缓冲区首帧，成功后按帧长从缓冲区移除（SERIALIZED_HEADER_SIZE + header.length）。
    // - 重同步机制：连续解析失败达阈值时，小步丢弃1字节尝试重同步，避免异常数据卡死；仍保留最大包长防护。
    if (!m_socket) {
        return;
    }

    QByteArray data = m_socket->readAll();
    m_bytesReceived += data.size();
    m_receiveBuffer.append(data);

    constexpr int kMaxResyncAttempts = 4; // 小阈值，避免过度丢弃

    for (;;) {
        if (m_receiveBuffer.size() < static_cast<qsizetype>(SERIALIZED_HEADER_SIZE)) {
            // 头部都不够，等待更多数据
            break;
        }

        MessageHeader header;
        QByteArray payload;
        const bool ok = Protocol::parseMessage(m_receiveBuffer, header, payload);
        if (!ok) {
            // 可能是半包，也可能是错误；采用重同步策略
            m_parseFailCount++;
            if (m_parseFailCount >= kMaxResyncAttempts) {
                // 小步前进尝试找到边界
                m_receiveBuffer.remove(0, 1);
                m_parseFailCount = 0;
                continue;
            }
            // 更可能是半包，等待更多数据
            break;
        }

        // 解析成功，重置失败计数
        m_parseFailCount = 0;

        // 额外防护：限制最大负载长度，避免超大包导致内存问题
        if (header.length > static_cast<quint32>(NetworkConstants::MAX_PACKET_SIZE)) {
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager)
                << "Payload too large, length:" << header.length
                << "from client:" << m_clientId;
            disconnectClient();
            return;
        }

        // 分发处理
        processMessage(header, payload);
        emit messageReceived(header.type, payload);

        // 移除已处理的密文段（XOR 加解密不改变长度）
        const qsizetype consumed = static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) + static_cast<qsizetype>(header.length);
        if (consumed > 0 && consumed <= m_receiveBuffer.size()) {
            m_receiveBuffer.remove(0, consumed);
        } else {
            // 理论不应发生，防御性处理
            m_receiveBuffer.clear();
            break;
        }
    }

    m_lastHeartbeat = QDateTime::currentDateTime();
}

void ClientHandler::onDisconnected()
{
    m_heartbeatTimer->stop();
    m_heartbeatCheckTimer->stop();
    
    // 清理接收缓冲区，避免残留数据导致协议解析错误
    m_receiveBuffer.clear();
    
    emit disconnected();
}

void ClientHandler::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    QString errorMsg;
    if (m_socket) {
        // 将常见的英文错误信息翻译为中文
        QString originalError = m_socket->errorString();
        if (originalError.contains("remote host closed", Qt::CaseInsensitive)) {
            errorMsg = "远程主机关闭了连接";
        } else if (originalError.contains("connection refused", Qt::CaseInsensitive)) {
            errorMsg = "连接被拒绝";
        } else if (originalError.contains("host not found", Qt::CaseInsensitive)) {
            errorMsg = "找不到主机";
        } else if (originalError.contains("network unreachable", Qt::CaseInsensitive)) {
            errorMsg = "网络不可达";
        } else if (originalError.contains("timeout", Qt::CaseInsensitive)) {
            errorMsg = "连接超时";
        } else {
            errorMsg = originalError; // 保留原始错误信息
        }
    } else {
        errorMsg = "未知错误";
    }
    emit errorOccurred(errorMsg);
}

void ClientHandler::sendHeartbeat()
{
    sendMessage(MessageType::HEARTBEAT, BaseMessage());
}

void ClientHandler::checkHeartbeat()
{
    if (m_lastHeartbeat.secsTo(QDateTime::currentDateTime()) > NetworkConstants::HEARTBEAT_TIMEOUT / 1000) {
        emit errorOccurred("Heartbeat timeout");
        disconnectClient();
    }
}

void ClientHandler::processMessage(const MessageHeader &header, const QByteArray &payload)
{
    switch (header.type) {
        case MessageType::HANDSHAKE_REQUEST:
            handleHandshakeRequest(payload);
            break;
        case MessageType::AUTHENTICATION_REQUEST:
            handleAuthenticationRequest(payload);
            break;
        case MessageType::HEARTBEAT:
            handleHeartbeat();
            break;
        case MessageType::MOUSE_EVENT:
            handleMouseEvent(payload);
            break;
        case MessageType::KEYBOARD_EVENT:
            handleKeyboardEvent(payload);
            break;
        case MessageType::DISCONNECT_REQUEST:
            handleDisconnectRequest();
            break;
        default:
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Unhandled message type:" << static_cast<quint32>(header.type);
            break;
    }
}

void ClientHandler::handleHandshakeRequest(const QByteArray &data)
{
    Q_UNUSED(data)
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServerManager) << "Received handshake request from client:" << m_clientId;
    
    // 发送握手响应
    sendHandshakeResponse();
}

void ClientHandler::handleAuthenticationRequest(const QByteArray &data)
{
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServerManager) << "Received authentication request from client:" << m_clientId;

    AuthenticationRequest req;
    if (!req.decode(data)) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Invalid authentication request payload from" << m_clientId;
        sendAuthenticationResponse(AuthResult::UNKNOWN_ERROR);
        return;
    }

    QString username = QString::fromUtf8(req.username);
    Q_UNUSED(username);
    const QString clientField = QString::fromUtf8(req.passwordHash);

    // 若服务端未配置摘要，允许空口令通过，否则要求挑战流程
    if (m_expectedSalt.isEmpty() || m_expectedDigest.isEmpty()) {
        bool ok = clientField.isEmpty();
        if (ok) {
            m_isAuthenticated = true;
            m_failedAuthCount = 0;
            sendAuthenticationResponse(AuthResult::SUCCESS, generateSessionId());
            emit authenticated();
        } else {
            m_failedAuthCount++;
            sendAuthenticationResponse(AuthResult::INVALID_PASSWORD);
        }
        return;
    }

    // 如果客户端字段为空，则下发挑战参数（PBKDF2）
    if (clientField.isEmpty()) {
        AuthChallenge ch{};
        ch.method = 1u;
        ch.iterations = m_pbkdf2Iterations;
        ch.keyLength = m_pbkdf2KeyLength;
        memset(ch.saltHex, 0, sizeof(ch.saltHex));
        QByteArray saltHex = m_expectedSalt.toHex();
        int sc = qMin(saltHex.size(), static_cast<int>(sizeof(ch.saltHex) - 1));
        memcpy(ch.saltHex, saltHex.constData(), sc);
        ch.saltHex[sc] = '\0';
        sendMessage(MessageType::AUTH_CHALLENGE, ch);
        return;
    }

    // 客户端应回传派生值hex
    QByteArray providedDeriv = QByteArray::fromHex(clientField.toUtf8());
    if (providedDeriv.size() != m_expectedDigest.size()) {
        m_failedAuthCount++;
        sendAuthenticationResponse(AuthResult::INVALID_PASSWORD);
        return;
    }

    // 常量时间比较
    volatile uchar diff = 0;
    for (int i = 0; i < providedDeriv.size(); ++i) diff |= uchar(providedDeriv[i] ^ m_expectedDigest[i]);
    if (diff == 0) {
        m_isAuthenticated = true;
        m_failedAuthCount = 0;
        sendAuthenticationResponse(AuthResult::SUCCESS, generateSessionId());
        emit authenticated();
        return;
    }

    m_failedAuthCount++;
    sendAuthenticationResponse(AuthResult::INVALID_PASSWORD);
    if (m_failedAuthCount >= NetworkConstants::MAX_RETRY_COUNT) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Too many failed auth attempts from" << m_clientId << ", disconnecting";
        disconnectClient();
    }
}

void ClientHandler::setExpectedPasswordDigest(const QByteArray &salt, const QByteArray &digest)
{
    m_expectedSalt = salt;
    m_expectedDigest = digest;
}

void ClientHandler::handleHeartbeat()
{
    m_lastHeartbeat = QDateTime::currentDateTime();
    // 发送心跳响应
    sendHeartbeat();
}

void ClientHandler::handleDisconnectRequest()
{
    disconnectClient();
}

void ClientHandler::handleMouseEvent(const QByteArray &data)
{
    if (!m_isAuthenticated) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Received mouse event from unauthenticated client:" << m_clientId;
        return;
    }
    
    MouseEvent mouseEvent;
    if (!mouseEvent.decode(data)) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Failed to deserialize mouse event from client:" << m_clientId;
        return;
    }
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcServerManager) << "Received mouse event from client:" << m_clientId 
             << "Position:" << mouseEvent.x << "," << mouseEvent.y
             << "Buttons:" << mouseEvent.buttons
             << "Type:" << static_cast<int>(mouseEvent.eventType);
    
    // 使用输入模拟器执行鼠标操作
      if (m_inputSimulator && m_inputSimulator->isInitialized()) {
          switch (mouseEvent.eventType) {
              case MouseEventType::MOVE:
                  m_inputSimulator->simulateMouseMove(mouseEvent.x, mouseEvent.y);
                  break;
              case MouseEventType::LEFT_PRESS:
                  m_inputSimulator->simulateMousePress(mouseEvent.x, mouseEvent.y, Qt::LeftButton);
                  break;
              case MouseEventType::LEFT_RELEASE:
                  m_inputSimulator->simulateMouseRelease(mouseEvent.x, mouseEvent.y, Qt::LeftButton);
                  break;
              case MouseEventType::RIGHT_PRESS:
                  m_inputSimulator->simulateMousePress(mouseEvent.x, mouseEvent.y, Qt::RightButton);
                  break;
              case MouseEventType::RIGHT_RELEASE:
                  m_inputSimulator->simulateMouseRelease(mouseEvent.x, mouseEvent.y, Qt::RightButton);
                  break;
              case MouseEventType::MIDDLE_PRESS:
                  m_inputSimulator->simulateMousePress(mouseEvent.x, mouseEvent.y, Qt::MiddleButton);
                  break;
              case MouseEventType::MIDDLE_RELEASE:
                  m_inputSimulator->simulateMouseRelease(mouseEvent.x, mouseEvent.y, Qt::MiddleButton);
                  break;
              case MouseEventType::WHEEL_UP:
                  m_inputSimulator->simulateMouseWheel(mouseEvent.x, mouseEvent.y, 120);
                  break;
              case MouseEventType::WHEEL_DOWN:
                  m_inputSimulator->simulateMouseWheel(mouseEvent.x, mouseEvent.y, -120);
                  break;
              default:
                  QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Unknown mouse event type:" << static_cast<int>(mouseEvent.eventType);
                  break;
          }
      }
}

void ClientHandler::handleKeyboardEvent(const QByteArray &data)
{
    if (!m_isAuthenticated) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Received keyboard event from unauthenticated client:" << m_clientId;
        return;
    }
    
    KeyboardEvent keyEvent;
    if (!keyEvent.decode(data)) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Failed to deserialize keyboard event from client:" << m_clientId;
        return;
    }
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcServerManager) << "Received keyboard event from client:" << m_clientId
             << "KeyCode:" << keyEvent.keyCode
             << "Modifiers:" << keyEvent.modifiers
             << "Type:" << static_cast<int>(keyEvent.eventType)
             << "Text:" << QString::fromUtf8(keyEvent.text);
    
    // 使用输入模拟器执行键盘操作
     if (m_inputSimulator && m_inputSimulator->isInitialized()) {
         Qt::KeyboardModifiers modifiers = static_cast<Qt::KeyboardModifiers>(keyEvent.modifiers);
         
         switch (keyEvent.eventType) {
             case KeyboardEventType::KEY_PRESS:
                 m_inputSimulator->simulateKeyPress(keyEvent.keyCode, modifiers);
                 break;
             case KeyboardEventType::KEY_RELEASE:
                 m_inputSimulator->simulateKeyRelease(keyEvent.keyCode, modifiers);
                 break;
             default:
                 QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcServerManager) << "Unknown keyboard event type:" << static_cast<int>(keyEvent.eventType);
                 break;
         }
     }
}

void ClientHandler::sendHandshakeResponse()
{
    HandshakeResponse response;
    response.serverVersion = PROTOCOL_VERSION;
    response.screenWidth = 1920;
    response.screenHeight = 1080;
    response.colorDepth = 32;
    response.supportedFeatures = 0xFF; // 支持所有功能
    strcpy(response.serverName, "QtRemoteDesktop Server");
    strcpy(response.serverOS, "macOS");
    
    sendMessage(MessageType::HANDSHAKE_RESPONSE, response);
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServerManager) << "Sent handshake response to client:" << m_clientId;
}

void ClientHandler::sendAuthenticationResponse(AuthResult result, const QString &sessionId)
{
    AuthenticationResponse resp{};
    resp.result = result;
    memset(resp.sessionId, 0, sizeof(resp.sessionId));
    QByteArray sid = sessionId.toUtf8();
    int sc = qMin(sid.size(), static_cast<int>(sizeof(resp.sessionId) - 1));
    memcpy(resp.sessionId, sid.constData(), sc);
    resp.sessionId[sc] = '\0';
    resp.permissions = 0u;
    sendMessage(MessageType::AUTHENTICATION_RESPONSE, resp);
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcServerManager) << "Sent authentication response to client:" << m_clientId << "Result:" << static_cast<int>(result);
}

QString ClientHandler::generateSessionId() const
{
    return QString("session_%1").arg(QDateTime::currentMSecsSinceEpoch());
}