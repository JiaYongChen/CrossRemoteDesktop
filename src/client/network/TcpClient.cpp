#include "TcpClient.h"
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTimer>
#include "../common/core/config/MessageConstants.h"
#include <QtNetwork/QHostAddress>
#include <QtCore/QDebug>
#include "../common/core/logging/LoggingCategories.h"
#include <QtCore/QDataStream>
#include <QtCore/QMessageLogger>
#include <QtNetwork/QNetworkProxy>

TcpClient::TcpClient(QObject* parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_heartbeatCheckTimer(new QTimer(this)) {

    m_socket->setProxy(QNetworkProxy::NoProxy);

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

QString TcpClient::serverAddress() const {
    return m_hostName;
}

quint16 TcpClient::serverPort() const {
    return m_port;
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

    // 清理接收缓冲区
    m_receiveBuffer.clear();

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
    // 心跳消息特殊处理（底层网络层直接处理）
    if (header.type == MessageType::HEARTBEAT) {
        handleHeartbeat();
        return;
    }
    
    // 其他所有消息都发送给上层（ConnectionManager）处理
    emit messageReceived(header.type, payload);
}

void TcpClient::handleHeartbeat() {
    // 收到服务端的心跳请求，更新最后心跳时间并发送响应
    m_lastHeartbeat = QDateTime::currentDateTime();

    // 发送心跳响应
    sendMessage(MessageType::HEARTBEAT_RESPONSE, BaseMessage());

    qCDebug(lcClient) << "收到服务端心跳请求，已发送响应";
}

void TcpClient::checkHeartbeat() {
    if ( m_lastHeartbeat.secsTo(QDateTime::currentDateTime()) > NetworkConstants::HEARTBEAT_TIMEOUT / 1000 ) {
        emit errorOccurred("心跳超时");
        disconnectFromHost();
    }
}