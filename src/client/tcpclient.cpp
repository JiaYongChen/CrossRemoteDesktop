#include "tcpclient.h"
#include "../core/messageconstants.h"
#include "../core/compression.h"
#include <QHostAddress>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDebug>
#include <QDataStream>
#include <QPixmap>

TcpClient::TcpClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_hostName()
    , m_port(0)
    , m_sessionId()

    , m_username()
    , m_password()
    , m_connectionTimer(new QTimer(this))
    , m_heartbeatTimer(new QTimer(this))
    , m_connectionTimeout(DEFAULT_CONNECTION_TIMEOUT)
    , m_frameDataMutex()
{
    // 设置连接超时定时器
    m_connectionTimer->setSingleShot(true);
    connect(m_connectionTimer, &QTimer::timeout, this, &TcpClient::onConnectionTimeout);
    

    
    // 设置心跳定时器
    m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &TcpClient::sendHeartbeat);
    
    // 连接socket信号
    connect(m_socket, &QTcpSocket::connected, this, &TcpClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TcpClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpClient::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &TcpClient::onError);
}

TcpClient::~TcpClient()
{
    disconnectFromHost();
}

void TcpClient::connectToHost(const QString &hostName, quint16 port)
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        qDebug() << MessageConstants::Network::ALREADY_CONNECTED;
        return;
    }
    
    m_hostName = hostName;
    m_port = port;
    
    m_socket->connectToHost(hostName, port);
    m_connectionTimer->start(m_connectionTimeout);
}

void TcpClient::disconnectFromHost()
{
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        return;
    }
    
    m_connectionTimer->stop();

    m_heartbeatTimer->stop();
    
    // 清理接收缓冲区
    m_receiveBuffer.clear();
    
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        // 使用abort()强制断开连接，避免阻塞等待
        m_socket->abort();
    }
    

}

void TcpClient::abort()
{
    m_connectionTimer->stop();

    m_heartbeatTimer->stop();
    
    // 清理接收缓冲区
    m_receiveBuffer.clear();
    
    m_socket->abort();

}



bool TcpClient::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

bool TcpClient::isAuthenticated() const
{
    return !m_sessionId.isEmpty() && isConnected();
}

QString TcpClient::serverAddress() const
{
    return m_hostName;
}

quint16 TcpClient::serverPort() const
{
    return m_port;
}

QString TcpClient::sessionId() const
{
    return m_sessionId;
}

void TcpClient::authenticate(const QString &username, const QString &password)
{
    if (!isConnected()) {
        qDebug() << MessageConstants::Network::NOT_CONNECTED;
        return;
    }
    
    m_username = username;
    m_password = password;
    
    sendAuthenticationRequest(username, password);
}

void TcpClient::sendMessage(MessageType type, const QByteArray &data)
{
    if (!isConnected()) {
        qDebug() << MessageConstants::Network::NOT_CONNECTED;
        return;
    }
    
    // 使用协议格式创建消息
    QByteArray message = Protocol::createMessage(type, data);
    
    m_socket->write(message);
}



void TcpClient::setConnectionTimeout(int msecs)
{
    m_connectionTimeout = msecs;
}

int TcpClient::connectionTimeout() const
{
    return m_connectionTimeout;
}





void TcpClient::onConnected()
{
    qDebug() << "TcpClient::onConnected - TCP connection established";
    m_connectionTimer->stop();
    
    // 设置TCP优化选项
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);  // TCP_NODELAY
    m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 256 * 1024);  // 256KB发送缓冲区
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 256 * 1024);  // 256KB接收缓冲区
    
    // 发送握手请求
    sendHandshakeRequest();
    
    // 启动心跳定时器
    m_heartbeatTimer->start();
    
    qDebug() << "TcpClient::onConnected - Emitting connected signal";
    emit connected();
}

void TcpClient::onDisconnected()
{
    m_connectionTimer->stop();
    m_heartbeatTimer->stop();
    
    // 清理接收缓冲区，避免下次连接时处理残留数据
    m_receiveBuffer.clear();
    
    emit disconnected();
}

void TcpClient::onReadyRead()
{
    QByteArray data = m_socket->readAll();
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
}

void TcpClient::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    
    m_connectionTimer->stop();
    
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

void TcpClient::onConnectionTimeout()
{
    // 停止连接定时器并中止连接
    m_connectionTimer->stop();
    m_socket->abort();
    emit errorOccurred("连接超时");
}



void TcpClient::sendHeartbeat()
{
    if (isConnected()) {
        sendMessage(MessageType::HEARTBEAT, QByteArray());
    }
}



void TcpClient::processMessage(const MessageHeader &header, const QByteArray &payload)
{
    switch (header.type) {
        case MessageType::HANDSHAKE_RESPONSE:
            handleHandshakeResponse(payload);
            break;
        case MessageType::AUTHENTICATION_RESPONSE:
            handleAuthenticationResponse(payload);
            break;
        case MessageType::HEARTBEAT:
            handleHeartbeat();
            break;
        case MessageType::ERROR_MESSAGE:
            handleErrorMessage(payload);
            break;
        case MessageType::STATUS_UPDATE:
            handleStatusUpdate(payload);
            break;
        case MessageType::DISCONNECT_REQUEST:
            handleDisconnectRequest();
            break;
        case MessageType::SCREEN_DATA:
            handleScreenData(payload);
            break;
        default:
            qDebug() << "Unhandled message type:" << static_cast<quint32>(header.type);
            break;
    }
}

void TcpClient::handleHandshakeResponse(const QByteArray &data)
{
    HandshakeResponse response;
    if (Protocol::deserialize(data, response)) {
        qDebug() << MessageConstants::Network::HANDSHAKE_RESPONSE_RECEIVED;
        qDebug() << "Server version:" << response.serverVersion;
        qDebug() << "Screen resolution:" << response.screenWidth << "x" << response.screenHeight;
        
        // 发送认证请求
        sendAuthenticationRequest(m_username.isEmpty() ? "guest" : m_username, 
                                m_password.isEmpty() ? "" : m_password);
    } else {
        qDebug() << "Failed to parse handshake response";
        emit errorOccurred("服务器握手响应无效");
    }
}

void TcpClient::handleAuthenticationResponse(const QByteArray &data)
{
    AuthenticationResponse response;
    if (Protocol::deserialize(data, response)) {
        qDebug() << MessageConstants::Network::AUTH_RESPONSE_RECEIVED;
        qDebug() << "Auth result:" << static_cast<int>(response.result);
        
        if (response.result == AuthResult::SUCCESS) {
            m_sessionId = QString::fromUtf8(response.sessionId);
            qDebug() << MessageConstants::Network::AUTH_SUCCESSFUL.arg(m_sessionId);

            qDebug() << "TcpClient::handleAuthenticationResponse - Emitting authenticated signal";
            emit authenticated();
        } else {
            QString errorMsg;
            switch (response.result) {
                case AuthResult::INVALID_PASSWORD:
                    errorMsg = "密码错误";
                    break;
                case AuthResult::ACCESS_DENIED:
                    errorMsg = "访问被拒绝";
                    break;
                case AuthResult::SERVER_FULL:
                    errorMsg = "服务器已满";
                    break;
                default:
                    errorMsg = "认证失败";
                    break;
            }
            emit errorOccurred(errorMsg);
        }
    } else {
        qDebug() << "Failed to parse authentication response";
        emit errorOccurred("服务器认证响应无效");
    }
}

void TcpClient::handleHeartbeat()
{
    // 简单实现，暂时不处理
    qDebug() << MessageConstants::Network::HEARTBEAT_RECEIVED;
}

void TcpClient::handleErrorMessage(const QByteArray &data)
{
    // 反序列化ErrorMessage结构体
    ErrorMessage errorMsg;
    if (Protocol::deserialize(data, errorMsg)) {
        QString errorText = QString::fromUtf8(errorMsg.errorText);
        qDebug() << "Received error message from server:" << errorText;
        emit errorOccurred(errorText);
    } else {
        qDebug() << "Failed to deserialize error message";
        emit errorOccurred("Unknown error occurred");
    }
}

void TcpClient::handleStatusUpdate(const QByteArray &data)
{
    QString statusMsg = QString::fromUtf8(data);
    qDebug() << "Received status update from server:" << statusMsg;
    
    // 发出状态更新信号给UI
    emit statusUpdated(statusMsg);
}

void TcpClient::handleDisconnectRequest()
{
    qDebug() << MessageConstants::Network::DISCONNECT_REQUEST_RECEIVED;
    
    // 服务器请求断开连接，客户端应该优雅地断开
    disconnectFromHost();
}

void TcpClient::handleScreenData(const QByteArray &data)
{
    QByteArray frameData;
    {
        QMutexLocker locker(&m_frameDataMutex);
        
        if (m_previousFrameData.isEmpty()) {
            // 第一帧，直接使用接收到的数据
            frameData = data;
            m_previousFrameData = data;
        } else {
            // 尝试作为差异数据处理
            QByteArray reconstructedData = Compression::applyDifference(m_previousFrameData, data);
            
            if (!reconstructedData.isEmpty()) {
                // 差异数据处理成功
                frameData = reconstructedData;
                m_previousFrameData = reconstructedData;
            } else {
                // 差异数据处理失败，可能是完整帧
                frameData = data;
                m_previousFrameData = data;
            }
        }
    }
    
    // 将处理后的字节数组转换为QPixmap
    QPixmap frame;
    bool loaded = false;
    
    // 优化图像加载：首先尝试最常见的JPEG格式
    if (frame.loadFromData(frameData, "JPEG")) {
        loaded = true;
    }
    // 如果JPEG失败，尝试PNG格式
    else if (frame.loadFromData(frameData, "PNG")) {
        loaded = true;
    }
    // 如果都失败，尝试自动检测格式
    else if (frame.loadFromData(frameData)) {
        loaded = true;
    }
    
    if (loaded) {
        // 发出信号，传递屏幕数据给UI
        emit screenDataReceived(frame);
    }
}

void TcpClient::sendHandshakeRequest()
{
    HandshakeRequest request;
    memset(&request, 0, sizeof(request));  // 清零整个结构体
    
    request.clientVersion = PROTOCOL_VERSION;
    request.screenWidth = 1920;
    request.screenHeight = 1080;
    request.colorDepth = 32;
    request.compressionLevel = 6;
    strcpy(request.clientName, "QtRemoteDesktop Client");
    strcpy(request.clientOS, getClientOS().toUtf8().constData());
    
    qDebug() << "HandshakeRequest struct size:" << sizeof(HandshakeRequest);
    qDebug() << "Expected size:" << (4+2+2+1+1+64+32);
    
    // 使用专门的HandshakeRequest序列化函数
    QByteArray requestData = Protocol::serialize(request);
    qDebug() << "Serialized data size:" << requestData.size();
    qDebug() << "Serialized data hex:" << requestData.toHex();
    
    // 计算校验和并打印
    quint32 checksum = Protocol::calculateChecksum(requestData);
    qDebug() << "Calculated checksum for serialized data:" << Qt::hex << checksum;
    
    // 验证序列化数据的前几个字节
    if (requestData.size() >= 4) {
        quint32 clientVersionFromData = *reinterpret_cast<const quint32*>(requestData.constData());
        qDebug() << "ClientVersion from serialized data:" << clientVersionFromData;
    }
    
    // 详细分析序列化数据
    qDebug() << "=== 详细分析序列化数据 ===";
    const char* data = requestData.constData();
    
    // clientVersion (4字节)
    quint32 cv = *reinterpret_cast<const quint32*>(data);
    qDebug() << "字节0-3 (clientVersion):" << QString::number(cv, 16) << "=" << cv;
    
    // screenWidth (2字节)
    quint16 sw = *reinterpret_cast<const quint16*>(data + 4);
    qDebug() << "字节4-5 (screenWidth):" << QString::number(sw, 16) << "=" << sw;
    
    // screenHeight (2字节)
    quint16 sh = *reinterpret_cast<const quint16*>(data + 6);
    qDebug() << "字节6-7 (screenHeight):" << QString::number(sh, 16) << "=" << sh;
    
    // colorDepth (1字节)
    quint8 cd = *reinterpret_cast<const quint8*>(data + 8);
    qDebug() << "字节8 (colorDepth):" << QString::number(cd, 16) << "=" << cd;
    
    // compressionLevel (1字节)
    quint8 cl = *reinterpret_cast<const quint8*>(data + 9);
    qDebug() << "字节9 (compressionLevel):" << QString::number(cl, 16) << "=" << cl;
    
    // clientName (64字节)
    QString clientName = QString::fromUtf8(data + 10, 64).trimmed();
    qDebug() << "字节10-73 (clientName):" << clientName;
    
    // clientOS (32字节)
    QString clientOS = QString::fromUtf8(data + 74, 32).trimmed();
    qDebug() << "字节74-105 (clientOS):" << clientOS;
    
    // 检查是否有额外字节
    if (requestData.size() > 106) {
        qDebug() << "额外字节:" << requestData.mid(106).toHex();
    }
    
    sendMessage(MessageType::HANDSHAKE_REQUEST, requestData);
    
    qDebug() << MessageConstants::Network::HANDSHAKE_REQUEST_SENT;
}

void TcpClient::sendAuthenticationRequest(const QString &username, const QString &password)
{
    AuthenticationRequest request;
    strcpy(request.username, username.toUtf8().constData());
    strcpy(request.passwordHash, hashPassword(password).toUtf8().constData());
    request.authMethod = 0; // 简单密码认证
    
    QByteArray requestData = Protocol::serialize(request);
    sendMessage(MessageType::AUTHENTICATION_REQUEST, requestData);
    
    qDebug() << MessageConstants::Network::AUTH_REQUEST_SENT.arg(username);
}

void TcpClient::sendDisconnectRequest()
{
    sendMessage(static_cast<MessageType>(4), QByteArray("disconnect"));
}





void TcpClient::resetConnection()
{
    m_receiveBuffer.clear();
    m_sessionId.clear();
}

QString TcpClient::hashPassword(const QString &password)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(password.toUtf8());
    return hash.result().toHex();
}

QString TcpClient::getClientName()
{
    return "QtRemoteDesktop";
}

QString TcpClient::getClientOS()
{
#ifdef Q_OS_WIN
    return "Windows";
#elif defined(Q_OS_MAC)
    return "macOS";
#elif defined(Q_OS_LINUX)
    return "Linux";
#else
    return "Unknown";
#endif
}

void TcpClient::sendMouseEvent(int x, int y, int buttons, int eventType)
{
    if (!isAuthenticated()) {
        return;
    }
    
    MouseEvent mouseEvent;
    mouseEvent.x = x;
    mouseEvent.y = y;
    mouseEvent.buttons = buttons;
    mouseEvent.eventType = static_cast<MouseEventType>(eventType);
    mouseEvent.wheelDelta = 0;
    
    QByteArray eventData = Protocol::serialize(mouseEvent);
    sendMessage(MessageType::MOUSE_EVENT, eventData);
}

void TcpClient::sendKeyboardEvent(int key, int modifiers, bool pressed, const QString &text)
{
    if (!isAuthenticated()) {
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
    
    QByteArray eventData = Protocol::serialize(keyEvent);
    sendMessage(MessageType::KEYBOARD_EVENT, eventData);
}

void TcpClient::sendWheelEvent(int x, int y, int delta, int orientation)
{
    Q_UNUSED(orientation)  // 抑制未使用参数警告
    
    if (!isAuthenticated()) {
        return;
    }
    
    MouseEvent wheelEvent;
    wheelEvent.x = x;
    wheelEvent.y = y;
    wheelEvent.buttons = 0;
    wheelEvent.eventType = delta > 0 ? MouseEventType::WHEEL_UP : MouseEventType::WHEEL_DOWN;
    wheelEvent.wheelDelta = delta;
    
    QByteArray eventData = Protocol::serialize(wheelEvent);
    sendMessage(MessageType::MOUSE_EVENT, eventData);
}