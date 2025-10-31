#include "TcpClient.h"
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTimer>
#include "../common/core/config/MessageConstants.h"
#include <QtNetwork/QHostAddress>
#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>
#include <QtCore/QDebug>
#include "../common/core/logging/LoggingCategories.h"
#include <QtCore/QDataStream>
#include <QtGui/QPixmap>
#include <QtCore/QMessageLogger>
#include "../common/core/crypto/Encryption.h"
#include <QtCore/QMutexLocker>
#include <QtConcurrent/QtConcurrent>

TcpClient::TcpClient(QObject* parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_heartbeatCheckTimer(new QTimer(this))
    , m_frameDataMutex(new QMutex())
    , m_errorStatsMutex(new QMutex()) {
    // 连接 socket 信号到本类槽函数
    connect(m_socket, &QTcpSocket::connected, this, &TcpClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TcpClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpClient::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
        this, &TcpClient::onError);

    // 心跳超时检查定时器设置 - 用于检测服务端心跳超时
    m_heartbeatCheckTimer->setInterval(NetworkConstants::HEARTBEAT_TIMEOUT);
    connect(m_heartbeatCheckTimer, &QTimer::timeout, this, &TcpClient::checkHeartbeat);
}

TcpClient::~TcpClient() {
    disconnectFromHost();

    // 清理互斥锁
    delete m_frameDataMutex;
    delete m_errorStatsMutex;
}

void TcpClient::connectToHost(const QString& hostName, quint16 port) {
    if ( m_socket->state() != QAbstractSocket::UnconnectedState ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << MessageConstants::Network::ALREADY_CONNECTED;
        return;
    }

    m_hostName = hostName;
    m_port = port;

    m_socket->connectToHost(hostName, port);
}

void TcpClient::disconnectFromHost() {
    if ( m_socket->state() == QAbstractSocket::UnconnectedState ) {
        return;
    }

    m_heartbeatCheckTimer->stop();

    // 清理接收缓冲区
    m_receiveBuffer.clear();

    // 如果仍处于连接状态，优雅断开连接
    if ( m_socket->state() == QAbstractSocket::ConnectedState ) {
        // 使用非阻塞的优雅断开
        m_socket->disconnectFromHost();

        // 兜底：若短时间内未能断开，使用abort强制关闭，避免长时间悬挂
        QTimer::singleShot(1000, this, [this]() {
            if ( m_socket && m_socket->state() != QAbstractSocket::UnconnectedState ) {
                m_socket->abort();
            }
        });
    } else if ( m_socket->state() != QAbstractSocket::UnconnectedState ) {
        // 非连接状态（例如正在连接/正在关闭），直接强制断开
        m_socket->abort();
    }
}

void TcpClient::abort() {
    m_heartbeatCheckTimer->stop();

    // 清理接收缓冲区
    m_receiveBuffer.clear();

    m_socket->abort();
}

bool TcpClient::isConnected() const {
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

bool TcpClient::isAuthenticated() const {
    return !m_sessionId.isEmpty() && isConnected();
}

QString TcpClient::serverAddress() const {
    return m_hostName;
}

quint16 TcpClient::serverPort() const {
    return m_port;
}

QString TcpClient::sessionId() const {
    return m_sessionId;
}

void TcpClient::authenticate(const QString& username, const QString& password) {
    if ( !isConnected() ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << MessageConstants::Network::NOT_CONNECTED;
        return;
    }

    m_username = username;
    m_password = password;

    sendAuthenticationRequest(username, password);
}

void TcpClient::sendMessage(MessageType type, const IMessageCodec& message) {
    if ( !isConnected() ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << MessageConstants::Network::NOT_CONNECTED;
        return;
    }

    // 使用编解码器创建帧
    QByteArray messageData = Protocol::createMessage(type, message);

    m_socket->write(messageData);
}

void TcpClient::onConnected() {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << "TcpClient::onConnected - TCP connection established";

    // 设置TCP优化选项
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, NetworkConstants::KEEP_ALIVE_ENABLED);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, NetworkConstants::TCP_NODELAY_ENABLED);
    m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, NetworkConstants::SOCKET_SEND_BUFFER_SIZE);
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, NetworkConstants::SOCKET_RECEIVE_BUFFER_SIZE);

    // 发送握手请求
    sendHandshakeRequest();

    // 启动心跳超时检查定时器（用于检测服务端心跳）
    m_lastHeartbeat = QDateTime::currentDateTime();
    m_heartbeatCheckTimer->start();

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "TcpClient::onConnected - Emitting connected signal";
    emit connected();
}

void TcpClient::onDisconnected() {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << "TcpClient::onDisconnected - TCP connection closed";

    // 停止心跳检查定时器
    m_heartbeatCheckTimer->stop();

    resetConnection();

    // 发出断开连接信号，通知上层应用
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "TcpClient::onDisconnected - Emitting disconnected signal";
    emit disconnected();
}

void TcpClient::onError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error)

        QString errorMsg;
    QString originalError;
    if ( m_socket ) {
        // 将常见的英文错误信息翻译为中文
        originalError = m_socket->errorString();
        if ( originalError.contains("remote host closed", Qt::CaseInsensitive) ) {
            errorMsg = "远程主机关闭了连接";
        } else if ( originalError.contains("connection refused", Qt::CaseInsensitive) ) {
            errorMsg = "连接被拒绝";
        } else if ( originalError.contains("host not found", Qt::CaseInsensitive) ) {
            errorMsg = "找不到主机";
        } else if ( originalError.contains("network unreachable", Qt::CaseInsensitive) ) {
            errorMsg = "网络不可达";
        } else if ( originalError.contains("timeout", Qt::CaseInsensitive) ) {
            errorMsg = "连接超时";
        } else {
            errorMsg = originalError;
        }
    } else {
        errorMsg = "未知错误";
        originalError = "Socket is null";
    }

    // 详细的错误日志记录
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
        << "TcpClient::onError - Socket error occurred:"
        << "Error code:" << static_cast<int>(error)
        << "Original message:" << originalError
        << "Translated message:" << errorMsg;

    // 记录网络错误统计
    QString errorDetails = QString("Socket error code: %1, description: %2")
        .arg(static_cast<int>(error)).arg(errorMsg);
    recordNetworkError(errorDetails);

    emit errorOccurred(errorMsg);
}

void TcpClient::onReadyRead() {
    if ( !m_socket ) {
        return;
    }

    // 一次性读取所有可用数据
    QByteArray newData = m_socket->readAll();
    if ( newData.isEmpty() ) {
        return;
    }

    // 检查缓冲区大小，防止无限增长
    if ( m_receiveBuffer.size() + newData.size() > NetworkConstants::MAX_PACKET_SIZE ) {
        qCCritical(lcClient) << "接收缓冲区超过最大限制:" << NetworkConstants::MAX_PACKET_SIZE
            << "当前大小:" << m_receiveBuffer.size()
            << "新增数据:" << newData.size();
        abort();
        return;
    }

    // 预留空间以减少内存分配次数
    m_receiveBuffer.reserve(m_receiveBuffer.size() + newData.size());
    m_receiveBuffer.append(newData);

    // 更新心跳时间
    m_lastHeartbeat = QDateTime::currentDateTime();

    // 处理缓冲区中的完整消息
    while ( !m_receiveBuffer.isEmpty() ) {
        // 步骤1：先验证数据完整性，同时获取MessageHeader
        MessageHeader header;
        QByteArray payload;
        qsizetype result = Protocol::parseMessage(m_receiveBuffer, header, payload);
        if ( result > 0 ) {
            // 步骤3：移除已处理的数据
            m_receiveBuffer.remove(0, result);

            // 步骤4：异步处理消息，使用 QMetaObject::invokeMethod 调度到主线程
            // 这样可以避免跨线程访问 QTcpSocket 的问题
            QMetaObject::invokeMethod(this, [this, header, payload]() {
                processMessage(header, payload);
            }, Qt::QueuedConnection);
        } else if ( result == 0 ) {
            // 消息无效，清空缓冲区
            qCCritical(lcClient) << "接收到无效消息，清空缓冲区";
            m_receiveBuffer.clear();
        } else {
            // 数据不完整，等待更多数据
            break;
        }
    }
}

void TcpClient::processMessage(const MessageHeader& header, const QByteArray& payload) {
    switch ( header.type ) {
        case MessageType::HANDSHAKE_RESPONSE:
            handleHandshakeResponse(payload);
            break;
        case MessageType::AUTHENTICATION_RESPONSE:
            handleAuthenticationResponse(payload);
            break;
        case MessageType::AUTH_CHALLENGE:
        {
            AuthChallenge ch{};
            if ( ch.decode(payload) ) {
                QByteArray salt = QByteArray::fromHex(QByteArray(ch.saltHex));
                if ( !salt.isEmpty() ) {
                    // 本地派生 PBKDF2-SHA256
                    QByteArray derived = HashGenerator::pbkdf2(m_password.toUtf8(), salt, int(ch.iterations), int(ch.keyLength));
                    QString hex = derived.toHex();
                    // 构造 AuthenticationRequest 并序列化发送
                    AuthenticationRequest ar{};
                    QByteArray uname = (m_username.isEmpty() ? QString("guest") : m_username).toUtf8();
                    int uc = qMin(uname.size(), static_cast<int>(sizeof(ar.username) - 1));
                    memcpy(ar.username, uname.constData(), uc);
                    ar.username[uc] = '\0';
                    QByteArray hexBytes = hex.toUtf8();
                    int hc = qMin(hexBytes.size(), static_cast<int>(sizeof(ar.passwordHash) - 1));
                    memcpy(ar.passwordHash, hexBytes.constData(), hc);
                    ar.passwordHash[hc] = '\0';
                    ar.authMethod = 1u;
                    sendMessage(MessageType::AUTHENTICATION_REQUEST, ar);
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
        case MessageType::SCREEN_DATA:
            handleScreenData(payload);
            break;
        default:
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "Unhandled message type:" << static_cast<quint32>(header.type);
            break;
    }
}

void TcpClient::handleHandshakeResponse(const QByteArray& data) {
    HandshakeResponse response;
    if ( response.decode(data) ) {
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

void TcpClient::handleAuthenticationResponse(const QByteArray& data) {
    AuthenticationResponse response;
    if ( response.decode(data) ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << MessageConstants::Network::AUTH_RESPONSE_RECEIVED;
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "Auth result:" << static_cast<int>(response.result);

        if ( response.result == AuthResult::SUCCESS ) {
            m_sessionId = QString::fromUtf8(response.sessionId);
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << MessageConstants::Network::AUTH_SUCCESSFUL.arg(m_sessionId);

            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "TcpClient::handleAuthenticationResponse - Emitting authenticated signal";
            emit authenticated();
        } else {
            QString errorMsg;
            switch ( response.result ) {
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

void TcpClient::handleHeartbeat() {
    // 收到服务端的心跳请求，更新最后心跳时间并发送响应
    m_lastHeartbeat = QDateTime::currentDateTime();

    // 发送心跳响应
    sendMessage(MessageType::HEARTBEAT_RESPONSE, BaseMessage());

    qDebug() << "收到服务端心跳请求，已发送响应";
}

void TcpClient::handleErrorMessage(const QByteArray& data) {
    // 字段级解码 ErrorMessage
    ErrorMessage errorMsg{};
    if ( errorMsg.decode(data) ) {
        QString errorText = QString::fromUtf8(errorMsg.errorText);
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "Received error message from server:" << errorText;
        emit errorOccurred(errorText);
    } else {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "Failed to deserialize error message";
        emit errorOccurred("Unknown error occurred");
    }
}

void TcpClient::handleStatusUpdate(const QByteArray& data) {
    // 使用结构化解码
    StatusUpdate st{};
    if ( st.decode(data) ) {
        QString statusMsg = QString("状态:%1  收:%2  发:%3  FPS:%4  CPU:%5%%  MEM:%6")
            .arg(st.connectionStatus)
            .arg(st.bytesReceived)
            .arg(st.bytesSent)
            .arg(st.fps)
            .arg(st.cpuUsage)
            .arg(st.memoryUsage);
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcClient) << "Received status update:" << statusMsg;
        emit statusUpdated(statusMsg);
    } else {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient) << "Failed to decode status update";
    }
}

/**
 * @brief 处理接收到的屏幕数据
 * @param data 原始屏幕数据字节数组
 *
 * 该方法负责解码服务器发送的ScreenData格式数据，
 * 提取图像数据并转换为QImage格式供UI显示使用。
 *
 * 数据格式说明：
 * - Server端发送JPEG格式的压缩图像数据
 * - 数据已在DataProcessingWorker中编码为JPEG格式（quality=85）
 * - 使用QImage::loadFromData直接加载JPEG数据
 * - JPEG格式提供良好的压缩率，减少网络传输量
 */
void TcpClient::handleScreenData(const QByteArray& data) {
    // 更新总帧数统计
    {
        QMutexLocker locker(m_errorStatsMutex);
        m_errorStats.totalFramesReceived++;
    }

    // 使用正确的ScreenData结构体解码数据
    ScreenData screenData{};
    if ( !screenData.decode(data) ) {
        QString errorDetails = QString("Data size: %1, expected minimum: 14 bytes")
            .arg(data.size());
        recordDecodeFailure(errorDetails);
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
            << "Failed to decode ScreenData from received data, size:" << data.size();

        return;
    }

    // 验证数据完整性
    if ( screenData.imageData.isEmpty() || screenData.dataSize == 0 ) {
        QString errorDetails = QString("Empty image data - dataSize: %1, imageData size: %2")
            .arg(screenData.dataSize).arg(screenData.imageData.size());
        recordDataCorruption(errorDetails);
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
            << "ScreenData contains empty image data";

        return;
    }

    if ( static_cast<quint32>(screenData.imageData.size()) != screenData.dataSize ) {
        QString errorDetails = QString("Size mismatch - expected: %1, actual: %2")
            .arg(screenData.dataSize).arg(screenData.imageData.size());
        recordDataCorruption(errorDetails);
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
    QImage frame;
    bool loaded = frame.loadFromData(frameData, "JPEG");

    if ( loaded && !frame.isNull() ) {
        // 成功加载图像
        // qCDebug(lcClient) << "JPEG图像加载成功，尺寸:" << frame.width() << "x" << frame.height()
        //     << "格式:" << frame.format()
        //     << "压缩数据大小:" << frameData.size() << "bytes";

        // 发出信号，传递屏幕数据给UI（QImage，线程安全）
        emit screenDataReceived(frame);
    } else {
        QString errorDetails = QString("Frame data size: %1, dimensions: %2x%3, JPEG header: %4")
            .arg(frameData.size())
            .arg(screenData.width)
            .arg(screenData.height)
            .arg(QString(frameData.left(16).toHex()));
        recordImageLoadFailure(errorDetails);
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
            << "Failed to load JPEG image from frame data, size:" << frameData.size()
            << "first 16 bytes:" << frameData.left(16).toHex();
    }
}

void TcpClient::checkHeartbeat() {
    if ( m_lastHeartbeat.secsTo(QDateTime::currentDateTime()) > NetworkConstants::HEARTBEAT_TIMEOUT / 1000 ) {
        emit errorOccurred("心跳超时");
        disconnectFromHost();
    }
}

void TcpClient::sendHandshakeRequest() {
    HandshakeRequest request{};

    request.clientVersion = PROTOCOL_VERSION;
    request.screenWidth = 1920;
    request.screenHeight = 1080;
    request.colorDepth = 32;
    strcpy(request.clientName, "QtRemoteDesktop Client");
    strcpy(request.clientOS, getClientOS().toUtf8().constData());

    sendMessage(MessageType::HANDSHAKE_REQUEST, request);

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << MessageConstants::Network::HANDSHAKE_REQUEST_SENT;
}

void TcpClient::sendAuthenticationRequest(const QString& username, const QString& password) {
    Q_UNUSED(password);
    // 第一次发送不带hash，触发服务端下发挑战
    AuthenticationRequest ar{};
    QByteArray uname = username.toUtf8();
    int uc = qMin(uname.size(), static_cast<int>(sizeof(ar.username) - 1));
    memcpy(ar.username, uname.constData(), uc);
    ar.username[uc] = '\0';
    ar.passwordHash[0] = '\0';
    ar.authMethod = 1u; // 请求PBKDF2
    sendMessage(MessageType::AUTHENTICATION_REQUEST, ar);

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << MessageConstants::Network::AUTH_REQUEST_SENT.arg(username);
}

void TcpClient::resetConnection() {
    m_receiveBuffer.clear();
    m_sessionId.clear();
}

QString TcpClient::hashPassword(const QString& password) {
    // 已废弃（阶段C改为PBKDF2）。保留占位。
    Q_UNUSED(password);
    return QString();
}

QString TcpClient::getClientName() {
    return "QtRemoteDesktop";
}

QString TcpClient::getClientOS() {
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

/**
 * @brief 获取错误统计信息
 * @return 错误统计结构体
 */
TcpClient::ErrorStatistics TcpClient::getErrorStatistics() const {
    QMutexLocker locker(m_errorStatsMutex);
    return m_errorStats;
}

/**
 * @brief 记录解码失败错误
 * @param details 错误详情
 */
void TcpClient::recordDecodeFailure(const QString& details) {
    QMutexLocker locker(m_errorStatsMutex);
    m_errorStats.decodeFailures++;
    m_errorStats.lastErrorTime = QDateTime::currentDateTime();
    m_errorStats.lastErrorMessage = QString("Decode failure: %1").arg(details);

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
        << "Decode failure recorded:" << details
        << "Total decode failures:" << m_errorStats.decodeFailures;
}

/**
 * @brief 记录图像加载失败错误
 * @param details 错误详情
 */
void TcpClient::recordImageLoadFailure(const QString& details) {
    QMutexLocker locker(m_errorStatsMutex);
    m_errorStats.imageLoadFailures++;
    m_errorStats.lastErrorTime = QDateTime::currentDateTime();
    m_errorStats.lastErrorMessage = QString("Image load failure: %1").arg(details);

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
        << "Image load failure recorded:" << details
        << "Total image load failures:" << m_errorStats.imageLoadFailures;
}

/**
 * @brief 记录网络错误
 * @param details 错误详情
 */
void TcpClient::recordNetworkError(const QString& details) {
    QMutexLocker locker(m_errorStatsMutex);
    m_errorStats.networkErrors++;
    m_errorStats.lastErrorTime = QDateTime::currentDateTime();
    m_errorStats.lastErrorMessage = QString("Network error: %1").arg(details);

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
        << "Network error recorded:" << details
        << "Total network errors:" << m_errorStats.networkErrors;
}

/**
 * @brief 记录数据损坏错误
 * @param details 错误详情
 */
void TcpClient::recordDataCorruption(const QString& details) {
    QMutexLocker locker(m_errorStatsMutex);
    m_errorStats.dataCorruptions++;
    m_errorStats.lastErrorTime = QDateTime::currentDateTime();
    m_errorStats.lastErrorMessage = QString("Data corruption: %1").arg(details);

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcClient)
        << "Data corruption recorded:" << details
        << "Total data corruptions:" << m_errorStats.dataCorruptions;
}

void TcpClient::sendMouseEvent(int x, int y, int buttons, int eventType) {
    if ( !isAuthenticated() ) {
        return;
    }

    MouseEvent mouseEvent;
    mouseEvent.x = x;
    mouseEvent.y = y;
    mouseEvent.buttons = buttons;
    mouseEvent.eventType = static_cast<MouseEventType>(eventType);
    mouseEvent.wheelDelta = 0;

    sendMessage(MessageType::MOUSE_EVENT, mouseEvent);
}

void TcpClient::sendKeyboardEvent(int key, int modifiers, bool pressed, const QString& text) {
    if ( !isAuthenticated() ) {
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

    sendMessage(MessageType::KEYBOARD_EVENT, keyEvent);
}

void TcpClient::sendWheelEvent(int x, int y, int delta, int orientation) {
    Q_UNUSED(orientation)  // 抑制未使用参数警告

        if ( !isAuthenticated() ) {
            return;
        }

    MouseEvent wheelEvent;
    wheelEvent.x = x;
    wheelEvent.y = y;
    wheelEvent.buttons = 0;
    wheelEvent.eventType = delta > 0 ? MouseEventType::WHEEL_UP : MouseEventType::WHEEL_DOWN;
    wheelEvent.wheelDelta = delta;

    sendMessage(MessageType::MOUSE_EVENT, wheelEvent);
}

// 重试机制相关方法实现