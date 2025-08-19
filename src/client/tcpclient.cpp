#include "tcpclient.h"
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTimer>
#include "../core/messageconstants.h"
#include "../core/compression.h"
#include "../common/core/protocolcodec.h"
#include <QtNetwork/QHostAddress>
#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>
#include <QtCore/QDebug>
#include "../common/core/logging_categories.h"
#include <QtCore/QDataStream>
#include <QtGui/QPixmap>
#include <QtCore/QMessageLogger>
#include "../common/core/encryption.h"
#include <QtCore/QMutexLocker>

TcpClient::TcpClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_heartbeatTimer(new QTimer(this))
    , m_heartbeatCheckTimer(new QTimer(this))
    , m_codec(nullptr)
{
    // 连接 socket 信号到本类槽函数
    connect(m_socket, &QTcpSocket::connected, this, &TcpClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TcpClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpClient::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &TcpClient::onError);

    // 若未注入外部编解码器，则使用默认 ProtocolCodec
    if (!m_codec) {
        m_codec = new ProtocolCodec();
        m_codecOwned = true;
    }

    // 心跳定时器设置
    m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &TcpClient::sendHeartbeat);

    // 心跳超时检查定时器设置
    m_heartbeatCheckTimer->setInterval(HEARTBEAT_TIMEOUT);
    connect(m_heartbeatCheckTimer, &QTimer::timeout, this, &TcpClient::checkHeartbeat);
}

TcpClient::~TcpClient()
{
    disconnectFromHost();
    if (m_codecOwned) {
        delete m_codec;
        m_codec = nullptr;
        m_codecOwned = false;
    }
}

void TcpClient::setCodec(IMessageCodec *codec)
{
    setCodec(codec, false);
}

void TcpClient::setCodec(IMessageCodec *codec, bool takeOwnership)
{
    if (codec == m_codec) return;
    if (m_codecOwned && m_codec) {
        delete m_codec;
    }
    if (codec) {
        m_codec = codec;
        m_codecOwned = takeOwnership;
    } else {
        m_codec = new ProtocolCodec();
        m_codecOwned = true;
    }
}

void TcpClient::connectToHost(const QString &hostName, quint16 port)
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << MessageConstants::Network::ALREADY_CONNECTED;
        return;
    }
    
    m_hostName = hostName;
    m_port = port;
    
    m_socket->connectToHost(hostName, port);
}

void TcpClient::disconnectFromHost()
{
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        return;
    }
    
    m_heartbeatTimer->stop();
    m_heartbeatCheckTimer->stop();
    
    // 清理接收缓冲区
    m_receiveBuffer.clear();
    
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        // 使用abort()强制断开连接，避免阻塞等待
        m_socket->abort();
    }
}

void TcpClient::abort()
{
    m_heartbeatTimer->stop();
    m_heartbeatCheckTimer->stop();
    
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
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << MessageConstants::Network::NOT_CONNECTED;
        return;
    }
    
    m_username = username;
    m_password = password;
    
    sendAuthenticationRequest(username, password);
}

void TcpClient::sendMessage(MessageType type, const QByteArray &data)
{
    if (!isConnected()) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << MessageConstants::Network::NOT_CONNECTED;
        return;
    }
    
    // 使用编解码器创建帧
    QByteArray message = m_codec ? m_codec->encode(type, data) : Protocol::createMessage(type, data);
    
    m_socket->write(message);
}

void TcpClient::onConnected()
{
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << "TcpClient::onConnected - TCP connection established";
    
     // 设置TCP优化选项
     m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);  // TCP_NODELAY
     m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 256 * 1024);  // 256KB发送缓冲区
     m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 256 * 1024);  // 256KB接收缓冲区
     
     // 发送握手请求
     sendHandshakeRequest();
     
     // 启动心跳定时器
     m_heartbeatTimer->start();
     m_lastHeartbeat = QDateTime::currentDateTime();
     m_heartbeatCheckTimer->start();
     
     QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "TcpClient::onConnected - Emitting connected signal";
     emit connected();
 }

 void TcpClient::onDisconnected()
 {
     m_heartbeatTimer->stop();
     m_heartbeatCheckTimer->stop();
     
     resetConnection();
 }

 void TcpClient::onError(QAbstractSocket::SocketError error)
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
             errorMsg = originalError;
         }
     } else {
         errorMsg = "未知错误";
     }
     
     emit errorOccurred(errorMsg);
 }

void TcpClient::onReadyRead()
{
    QByteArray data = m_socket->readAll();
    m_receiveBuffer.append(data);
    m_lastHeartbeat = QDateTime::currentDateTime();
    
    // 使用编解码器解析消息
    while (m_receiveBuffer.size() >= static_cast<qsizetype>(SERIALIZED_HEADER_SIZE)) {
        const qsizetype before = m_receiveBuffer.size();
        MessageHeader header;
        QByteArray payload;
        bool ok = m_codec ? m_codec->tryDecode(m_receiveBuffer, header, payload)
                          : Protocol::parseMessage(m_receiveBuffer, header, payload);
        if (!ok) {
            break; // 等待更多数据或交由后续改进处理同步
        }
        processMessage(header, payload);
        emit messageReceived(header.type, payload);
        if (m_receiveBuffer.size() == before) {
            break;
        }
    }
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
        case MessageType::AUTH_CHALLENGE:
        {
            AuthChallenge ch{};
            if (Protocol::decodeAuthChallenge(payload, ch)) {
                QByteArray salt = QByteArray::fromHex(QByteArray(ch.saltHex));
                if (!salt.isEmpty()) {
                    // 本地派生 PBKDF2-SHA256
                    QByteArray derived = HashGenerator::pbkdf2(m_password.toUtf8(), salt, int(ch.iterations), int(ch.keyLength));
                    QString hex = derived.toHex();
                    // 回发认证请求：username + 派生值hex
                    QByteArray req = Protocol::encodeAuthenticationRequest(
                        m_username.isEmpty() ? "guest" : m_username,
                        hex,
                        1u
                    );
                    sendMessage(MessageType::AUTHENTICATION_REQUEST, req);
                }
            }
            break;
        }
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
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "Unhandled message type:" << static_cast<quint32>(header.type);
            break;
    }
}

void TcpClient::handleHandshakeResponse(const QByteArray &data)
{
    HandshakeResponse response;
    if (Protocol::decodeHandshakeResponse(data, response)) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << MessageConstants::Network::HANDSHAKE_RESPONSE_RECEIVED;
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "Server version:" << response.serverVersion;
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "Screen resolution:" << response.screenWidth << "x" << response.screenHeight;
        
        // 发送认证请求
        sendAuthenticationRequest(m_username.isEmpty() ? "guest" : m_username, 
                                m_password.isEmpty() ? "" : m_password);
    } else {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "Failed to parse handshake response";
        emit errorOccurred("服务器握手响应无效");
    }
}

void TcpClient::handleAuthenticationResponse(const QByteArray &data)
{
    AuthenticationResponse response;
    if (Protocol::decodeAuthenticationResponse(data, response)) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << MessageConstants::Network::AUTH_RESPONSE_RECEIVED;
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "Auth result:" << static_cast<int>(response.result);
        
        if (response.result == AuthResult::SUCCESS) {
            m_sessionId = QString::fromUtf8(response.sessionId);
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << MessageConstants::Network::AUTH_SUCCESSFUL.arg(m_sessionId);

            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "TcpClient::handleAuthenticationResponse - Emitting authenticated signal";
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
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "Failed to parse authentication response";
        emit errorOccurred("服务器认证响应无效");
    }
}

void TcpClient::handleHeartbeat()
{
    // 简单实现，暂时不处理
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << MessageConstants::Network::HEARTBEAT_RECEIVED;
    m_lastHeartbeat = QDateTime::currentDateTime();
}

void TcpClient::handleErrorMessage(const QByteArray &data)
{
    // 字段级解码 ErrorMessage
    ErrorMessage errorMsg{};
    if (Protocol::decodeErrorMessage(data, errorMsg)) {
        QString errorText = QString::fromUtf8(errorMsg.errorText);
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "Received error message from server:" << errorText;
        emit errorOccurred(errorText);
    } else {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "Failed to deserialize error message";
        emit errorOccurred("Unknown error occurred");
    }
}

void TcpClient::handleStatusUpdate(const QByteArray &data)
{
    // 优先尝试结构化解码，失败则兼容旧版字符串
    StatusUpdate st{};
    if (Protocol::decodeStatusUpdate(data, st)) {
        QString statusMsg = QString("状态:%1  收:%2  发:%3  FPS:%4  CPU:%5%%  MEM:%6")
                                .arg(st.connectionStatus)
                                .arg(st.bytesReceived)
                                .arg(st.bytesSent)
                                .arg(st.fps)
                                .arg(st.cpuUsage)
                                .arg(st.memoryUsage);
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "Received status update (structured):" << statusMsg;
        emit statusUpdated(statusMsg);
        return;
    }

    QString fallback = QString::fromUtf8(data);
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "Received status update (fallback string):" << fallback;
    emit statusUpdated(fallback);
}

void TcpClient::handleDisconnectRequest()
{
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << MessageConstants::Network::DISCONNECT_REQUEST_RECEIVED;
    
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
    
    // 将处理后的字节数组转换为QImage
    QImage frame;
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
        // 发出信号，传递屏幕数据给UI（QImage，线程安全）
        emit screenDataReceived(frame);
    }
}

void TcpClient::checkHeartbeat()
{
    if (m_lastHeartbeat.secsTo(QDateTime::currentDateTime()) > HEARTBEAT_TIMEOUT / 1000) {
        emit errorOccurred("心跳超时");
        disconnectFromHost();
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
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "HandshakeRequest struct size:" << sizeof(HandshakeRequest);
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "Expected size:" << (4+2+2+1+1+64+32);
    
    QByteArray requestData = Protocol::encodeHandshakeRequest(request);
    sendMessage(MessageType::HANDSHAKE_REQUEST, requestData);
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << MessageConstants::Network::HANDSHAKE_REQUEST_SENT;
}

void TcpClient::sendAuthenticationRequest(const QString &username, const QString &password)
{
    Q_UNUSED(password);
    // 第一次发送不带hash，触发服务端下发挑战
    QByteArray requestData = Protocol::encodeAuthenticationRequest(
        username,
        QString(),
        1u // 请求PBKDF2
    );
    sendMessage(MessageType::AUTHENTICATION_REQUEST, requestData);
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << MessageConstants::Network::AUTH_REQUEST_SENT.arg(username);
}

void TcpClient::sendDisconnectRequest()
{
    sendMessage(MessageType::DISCONNECT_REQUEST, QByteArray("disconnect"));
}

void TcpClient::resetConnection()
{
    m_receiveBuffer.clear();
    m_sessionId.clear();
}

QString TcpClient::hashPassword(const QString &password)
{
    // 已废弃（阶段C改为PBKDF2）。保留占位。
    Q_UNUSED(password);
    return QString();
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
    
    QByteArray eventData = Protocol::encodeMouseEvent(mouseEvent);
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
    
    QByteArray eventData = Protocol::encodeKeyboardEvent(keyEvent);
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
    
    QByteArray eventData = Protocol::encodeMouseEvent(wheelEvent);
    sendMessage(MessageType::MOUSE_EVENT, eventData);
}