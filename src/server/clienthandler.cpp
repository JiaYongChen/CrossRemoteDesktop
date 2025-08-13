#include "clienthandler.h"
#include "inputsimulator.h"
#include "../common/core/protocol.h"
#include "../common/core/networkconstants.h"
#include <QTcpSocket>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QDataStream>
#include <QBuffer>
#include <cstring>

// ClientHandler implementation
ClientHandler::ClientHandler(qintptr socketDescriptor, QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_clientPort(0)
    , m_isAuthenticated(false)
    , m_connectionTime(QDateTime::currentDateTime())
    , m_lastHeartbeat(QDateTime::currentDateTime())
    , m_heartbeatTimer(new QTimer(this))
    , m_heartbeatCheckTimer(new QTimer(this))
    , m_bytesReceived(0)
    , m_bytesSent(0)
    , m_inputSimulator(new InputSimulator(this))
{
    qDebug() << "[DEBUG] ClientHandler constructor called with socketDescriptor:" << socketDescriptor;
    if (!m_socket->setSocketDescriptor(socketDescriptor)) {
        qDebug() << "[DEBUG] Failed to set socket descriptor, error:" << m_socket->errorString();
        return;
    }
    qDebug() << "[DEBUG] Socket descriptor set successfully";
    
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
        qWarning() << "Failed to initialize input simulator for client:" << m_clientId;
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

void ClientHandler::sendMessage(MessageType type, const QByteArray &data)
{
    if (!isConnected()) {
        return;
    }
    
    // 使用协议格式创建消息
    QByteArray message = Protocol::createMessage(type, data);
    
    qint64 bytesWritten = m_socket->write(message);
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
    qDebug() << "Force disconnecting client:" << clientAddress();
    
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
        
        qDebug() << "Client forcefully disconnected:" << clientAddress();
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
    if (!m_socket) {
        return;
    }
    
    QByteArray data = m_socket->readAll();
    m_bytesReceived += data.size();
    m_receiveBuffer.append(data);
    
    // 按照协议处理消息
    while (m_receiveBuffer.size() >= static_cast<qsizetype>(SERIALIZED_HEADER_SIZE)) {
        MessageHeader header;
        QByteArray payload;
        
        if (Protocol::parseMessage(m_receiveBuffer, header, payload)) {
            // 移除已处理的数据
            qsizetype totalSize = static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) + header.length;
            m_receiveBuffer.remove(0, totalSize);
            
            // 处理消息
            processMessage(header, payload);
            emit messageReceived(header.type, payload);
        } else {
            // 检查是否是无效的协议数据
            if (m_receiveBuffer.size() >= static_cast<qsizetype>(SERIALIZED_HEADER_SIZE)) {
                MessageHeader tempHeader;
                QByteArray headerData = m_receiveBuffer.left(static_cast<qsizetype>(SERIALIZED_HEADER_SIZE));
                QDataStream stream(headerData);
                stream.setByteOrder(QDataStream::LittleEndian);
                stream >> tempHeader.magic;
                
                // 如果魔数无效，寻找下一个可能的协议起始位置
                if (tempHeader.magic != PROTOCOL_MAGIC) {
                    // 寻找PROTOCOL_MAGIC的字节序列
                    QByteArray magicBytes;
                    QDataStream magicStream(&magicBytes, QIODevice::WriteOnly);
                    magicStream.setByteOrder(QDataStream::LittleEndian);
                    magicStream << PROTOCOL_MAGIC;
                    
                    int nextMagicPos = m_receiveBuffer.indexOf(magicBytes, 1);
                    if (nextMagicPos > 0) {
                        // 找到下一个可能的协议起始位置，移除之前的无效数据
                        m_receiveBuffer.remove(0, nextMagicPos);
                    } else {
                        // 没找到有效的协议起始位置，移除第一个字节
                        m_receiveBuffer.remove(0, 1);
                    }
                    continue;
                }
            }
            // 数据不完整，等待更多数据
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
    sendMessage(MessageType::HEARTBEAT, QByteArray());
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
            qDebug() << "Unhandled message type:" << static_cast<quint32>(header.type);
            break;
    }
}

void ClientHandler::handleHandshakeRequest(const QByteArray &data)
{
    Q_UNUSED(data)
    qDebug() << "Received handshake request from client:" << m_clientId;
    
    // 发送握手响应
    sendHandshakeResponse();
}

void ClientHandler::handleAuthenticationRequest(const QByteArray &data)
{
    Q_UNUSED(data)
    qDebug() << "Received authentication request from client:" << m_clientId;
    
    // 简单认证，总是成功
    m_isAuthenticated = true;
    
    // 发送认证响应
    sendAuthenticationResponse(AuthResult::SUCCESS, generateSessionId());
    emit authenticated();
}

void ClientHandler::handleHeartbeat()
{
    m_lastHeartbeat = QDateTime::currentDateTime();
    // 发送心跳响应
    sendMessage(MessageType::HEARTBEAT, QByteArray());
}

void ClientHandler::handleDisconnectRequest()
{
    disconnectClient();
}

void ClientHandler::handleMouseEvent(const QByteArray &data)
{
    if (!m_isAuthenticated) {
        qWarning() << "Received mouse event from unauthenticated client:" << m_clientId;
        return;
    }
    
    MouseEvent mouseEvent;
    if (!Protocol::deserialize(data, mouseEvent)) {
        qWarning() << "Failed to deserialize mouse event from client:" << m_clientId;
        return;
    }
    
    qDebug() << "Received mouse event from client:" << m_clientId 
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
                  qWarning() << "Unknown mouse event type:" << static_cast<int>(mouseEvent.eventType);
                  break;
          }
      }
}

void ClientHandler::handleKeyboardEvent(const QByteArray &data)
{
    if (!m_isAuthenticated) {
        qWarning() << "Received keyboard event from unauthenticated client:" << m_clientId;
        return;
    }
    
    KeyboardEvent keyEvent;
    if (!Protocol::deserialize(data, keyEvent)) {
        qWarning() << "Failed to deserialize keyboard event from client:" << m_clientId;
        return;
    }
    
    qDebug() << "Received keyboard event from client:" << m_clientId
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
                 qWarning() << "Unknown keyboard event type:" << static_cast<int>(keyEvent.eventType);
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
    
    QByteArray responseData = Protocol::serialize(response);
    sendMessage(MessageType::HANDSHAKE_RESPONSE, responseData);
    
    qDebug() << "Sent handshake response to client:" << m_clientId;
}

void ClientHandler::sendAuthenticationResponse(AuthResult result, const QString &sessionId)
{
    AuthenticationResponse response;
    response.result = result;
    strcpy(response.sessionId, sessionId.toUtf8().constData());
    response.permissions = 0xFFFFFFFF; // 所有权限
    
    QByteArray responseData = Protocol::serialize(response);
    sendMessage(MessageType::AUTHENTICATION_RESPONSE, responseData);
    
    qDebug() << "Sent authentication response to client:" << m_clientId << "Result:" << static_cast<int>(result);
}

QString ClientHandler::generateSessionId() const
{
    return QString("session_%1").arg(QDateTime::currentMSecsSinceEpoch());
}