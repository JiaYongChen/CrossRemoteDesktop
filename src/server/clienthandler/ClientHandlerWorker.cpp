#include "ClientHandlerWorker.h"
#include "../simulator/InputSimulator.h"
#include "../dataflow/QueueManager.h"
#include "../dataflow/DataFlowStructures.h"
#include "../../common/core/network/Protocol.h"
#include "../../common/core/crypto/Encryption.h"
#include "../../common/core/config/NetworkConstants.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QDataStream>
#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QMessageLogger>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QRandomGenerator>
#include <cstring>

// 日志分类
Q_LOGGING_CATEGORY(clientHandlerWorker, "clienthandler.worker")

ClientHandlerWorker::ClientHandlerWorker(qintptr socketDescriptor, QObject* parent)
    : Worker(parent)
    , m_socketDescriptor(socketDescriptor)
    , m_socket(nullptr)
    , m_clientPort(0)
    , m_isAuthenticated(false)
    , m_failedAuthCount(0)
    , m_connectionTime(QDateTime::currentDateTime())
    , m_lastHeartbeat(QDateTime::currentDateTime())
    , m_heartbeatTimer(nullptr)
    , m_heartbeatCheckTimer(nullptr)
    , m_bytesReceived(0)
    , m_bytesSent(0)
    , m_inputSimulator(nullptr)
    , m_queueManager(nullptr) {
    qCDebug(clientHandlerWorker) << "ClientHandlerWorker 构造函数调用，套接字描述符:" << socketDescriptor;
    setName("ClientHandlerWorker");
}

ClientHandlerWorker::~ClientHandlerWorker() {
    qCDebug(clientHandlerWorker) << "ClientHandlerWorker 析构函数";

    // 确保Worker已停止
    if ( isRunning() ) {
        stop(true);
    }

    qCDebug(clientHandlerWorker) << "ClientHandlerWorker 析构完成";
}

bool ClientHandlerWorker::initialize() {
    qCInfo(clientHandlerWorker, "初始化 ClientHandlerWorker");

    // 在Worker线程中创建socket
    m_socket = new QTcpSocket(this);

    // 使用套接字描述符初始化socket
    if ( !m_socket->setSocketDescriptor(m_socketDescriptor) ) {
        qCCritical(clientHandlerWorker) << "无法设置套接字描述符:" << m_socket->errorString();
        delete m_socket;
        m_socket = nullptr;
        return false;
    }

    // 设置TCP优化选项
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);  // TCP_NODELAY
    m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 256 * 1024);  // 256KB发送缓冲区
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 256 * 1024);  // 256KB接收缓冲区

    // 获取客户端信息
    {
        QMutexLocker locker(&m_clientInfoMutex);
        m_clientAddress = m_socket->peerAddress().toString();
        m_clientPort = m_socket->peerPort();
        m_clientId = QString("%1:%2").arg(m_clientAddress).arg(m_clientPort);
    }

    // 连接套接字信号
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientHandlerWorker::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientHandlerWorker::onDisconnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
        this, &ClientHandlerWorker::onError);

    // 创建心跳检查定时器
    m_heartbeatCheckTimer = new QTimer(this);
    m_heartbeatCheckTimer->setInterval(NetworkConstants::HEARTBEAT_TIMEOUT);
    connect(m_heartbeatCheckTimer, &QTimer::timeout, this, &ClientHandlerWorker::checkHeartbeat);

    // 创建输入模拟器
    m_inputSimulator = new InputSimulator(this);
    if ( !m_inputSimulator->initialize() ) {
        qCWarning(clientHandlerWorker, "输入模拟器初始化失败，客户端: %s", qPrintable(clientId()));
    }

    // 获取队列管理器
    m_queueManager = QueueManager::instance();
    if ( !m_queueManager ) {
        qCWarning(clientHandlerWorker, "无法获取队列管理器实例");
    }

    // 启动心跳检查定时器
    m_heartbeatCheckTimer->start();

    qCInfo(clientHandlerWorker, "ClientHandlerWorker 初始化成功，客户端: %s", qPrintable(clientId()));

    return true;
}

void ClientHandlerWorker::cleanup() {
    qCInfo(clientHandlerWorker, "清理 ClientHandlerWorker 资源");

    // 停止定时器
    if ( m_heartbeatCheckTimer ) {
        m_heartbeatCheckTimer->stop();
    }

    if ( m_heartbeatTimer ) {
        m_heartbeatTimer->stop();
    }

    // 断开套接字连接
    if ( m_socket ) {
        m_socket->disconnectFromHost();
        if ( m_socket->state() != QAbstractSocket::UnconnectedState ) {
            m_socket->waitForDisconnected(3000);
        }
    }

    qCInfo(clientHandlerWorker, "ClientHandlerWorker 资源清理完成");
}

void ClientHandlerWorker::processTask() {
    // 在Worker线程中，主要的处理逻辑通过信号槽机制触发
    // 这里处理周期性任务：连接状态检查、数据接收、屏幕数据发送

    // 检查连接状态
    if ( m_socket && m_socket->state() != QAbstractSocket::ConnectedState ) {
        // 不要直接调用stop(),而是通过disconnected信号让ClientHandler来停止
        // 使用成员变量确保只触发一次
        if ( !m_disconnectSignalSent.exchange(true) ) {
            qCDebug(clientHandlerWorker) << "检测到连接断开(processTask)，触发disconnected信号";
            emit disconnected();
        }
        return;
    }

    // 处理接收缓冲区中的数据
    // readyRead信号已经连接到onReadyRead槽，这里只做额外检查
    // 如果有数据但信号未触发（罕见情况），手动触发处理
    if ( m_socket && m_socket->bytesAvailable() > 0 ) {
        // 使用异步调用避免阻塞processTask
        QMetaObject::invokeMethod(this, "onReadyRead", Qt::QueuedConnection);
    }

    // 认证成功后，异步从处理队列获取并发送屏幕数据
    // 使用QMetaObject::invokeMethod异步调用，避免阻塞processTask
    if ( isAuthenticated() && m_queueManager ) {
        QMetaObject::invokeMethod(this, "sendScreenDataFromQueue", Qt::QueuedConnection);
    }
}

void ClientHandlerWorker::sendScreenDataFromQueue() {
    auto processedQueue = m_queueManager->getProcessedQueue();
    if ( !processedQueue ) {
        return;
    }

    // 批量处理队列中的数据（每次最多处理指定数量）
    int processedCount = 0;

    while ( processedCount < m_maxFramesPerCycle ) {
        ProcessedData processedData;
        if ( !processedQueue->tryDequeue(processedData) ) {
            break; // 队列为空
        }

        // 创建ScreenData消息
        ScreenData screenData;
        screenData.x = 0;  // 全屏捕获，从坐标(0,0)开始
        screenData.y = 0;
        screenData.imageData = processedData.compressedData; // 存储原始像素数据
        screenData.width = processedData.imageSize.width();
        screenData.height = processedData.imageSize.height();
        screenData.dataSize = processedData.compressedData.size();

        // 预先编码消息,然后发送
        QByteArray messageData = Protocol::createMessage(MessageType::SCREEN_DATA, screenData);

        // 直接调用sendEncodedMessage(同步发送,在当前线程)
        sendEncodedMessage(messageData);

        processedCount++;
    }
}

QString ClientHandlerWorker::clientAddress() const {
    QMutexLocker locker(&m_clientInfoMutex);
    return m_clientAddress;
}

quint16 ClientHandlerWorker::clientPort() const {
    QMutexLocker locker(&m_clientInfoMutex);
    return m_clientPort;
}

QString ClientHandlerWorker::clientId() const {
    QMutexLocker locker(&m_clientInfoMutex);
    return m_clientId;
}

bool ClientHandlerWorker::isConnected() const {
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

bool ClientHandlerWorker::isAuthenticated() const {
    QMutexLocker locker(&m_clientInfoMutex);
    return m_isAuthenticated;
}

quint64 ClientHandlerWorker::bytesReceived() const {
    QMutexLocker locker(&m_statsMutex);
    return m_bytesReceived;
}

quint64 ClientHandlerWorker::bytesSent() const {
    QMutexLocker locker(&m_statsMutex);
    return m_bytesSent;
}

QDateTime ClientHandlerWorker::connectionTime() const {
    return m_connectionTime;
}

void ClientHandlerWorker::setExpectedPasswordDigest(const QByteArray& salt, const QByteArray& digest) {
    m_expectedSalt = salt;
    m_expectedDigest = digest;
}

void ClientHandlerWorker::setPbkdf2Params(quint32 iterations, quint32 keyLength) {
    m_pbkdf2Iterations = iterations;
    m_pbkdf2KeyLength = keyLength;
}

void ClientHandlerWorker::sendMessage(MessageType type, const IMessageCodec& message) {
    try {
        // 使用Protocol::createMessage来创建加密的消息
        QByteArray messageData = Protocol::createMessage(type, message);

        if ( messageData.isEmpty() ) {
            qCWarning(clientHandlerWorker, "消息数据为空，跳过发送");
            return;
        }

        // 调用统一的发送实现
        sendEncodedMessage(messageData);

        // 只在非屏幕数据消息时记录详细日志，避免高频日志输出
        if ( type != MessageType::SCREEN_DATA ) {
            qCDebug(clientHandlerWorker) << "消息发送完成: 类型=" << static_cast<int>(type)
                << ", 大小=" << messageData.size() << "bytes";
        }

    } catch ( const std::exception& e ) {
        qCWarning(clientHandlerWorker, "发送消息时发生异常: %s", e.what());
    } catch ( ... ) {
        qCWarning(clientHandlerWorker, "发送消息时发生未知异常");
    }
}

void ClientHandlerWorker::sendEncodedMessage(const QByteArray& messageData) {
    if ( !m_socket || m_socket->state() != QAbstractSocket::ConnectedState ) {
        qCWarning(clientHandlerWorker, "套接字未连接，无法发送消息");
        return;
    }

    if ( messageData.isEmpty() ) {
        qCWarning(clientHandlerWorker, "消息数据为空，跳过发送");
        return;
    }

    try {
        // 检查消息大小，对于大消息进行分块发送
        const qint64 CHUNK_SIZE = 64 * 1024; // 64KB分块
        const qint64 LARGE_MESSAGE_THRESHOLD = 1024 * 1024; // 1MB阈值

        qint64 totalSize = messageData.size();
        qint64 totalBytesSent = 0;

        if ( totalSize > LARGE_MESSAGE_THRESHOLD ) {
            // 只在发送大消息时记录日志
            qCDebug(clientHandlerWorker) << "发送大消息:" << totalSize << "bytes，使用分块发送";

            // 分块发送大消息
            qint64 offset = 0;
            while ( offset < totalSize ) {
                qint64 chunkSize = qMin(CHUNK_SIZE, totalSize - offset);
                qint64 bytesWritten = m_socket->write(messageData.constData() + offset, chunkSize);

                if ( bytesWritten == -1 ) {
                    qCWarning(clientHandlerWorker, "发送消息块失败: %s", qPrintable(m_socket->errorString()));
                    return;
                }

                if ( bytesWritten != chunkSize ) {
                    qCWarning(clientHandlerWorker, "消息块部分发送: 期望 %lld bytes，实际 %lld bytes", chunkSize, bytesWritten);
                }

                offset += bytesWritten;
                totalBytesSent += bytesWritten;

                // 等待数据写入完成，避免缓冲区溢出
                if ( !m_socket->waitForBytesWritten(5000) ) {
                    qCWarning(clientHandlerWorker, "等待数据写入超时");
                    break;
                }
            }
        } else {
            // 小消息直接发送
            qint64 bytesWritten = m_socket->write(messageData);

            if ( bytesWritten == -1 ) {
                qCWarning(clientHandlerWorker) << "发送消息失败:" << m_socket->errorString();
                return;
            }

            if ( bytesWritten != totalSize ) {
                qCWarning(clientHandlerWorker) << "消息部分发送: 期望" << totalSize << "bytes，实际" << bytesWritten << "bytes";
            }

            totalBytesSent = bytesWritten;
        }

        // 更新统计信息
        if ( totalBytesSent > 0 ) {
            QMutexLocker locker(&m_statsMutex);
            m_bytesSent += totalBytesSent;
        }

    } catch ( const std::exception& e ) {
        qCWarning(clientHandlerWorker, "发送消息时发生异常: %s", e.what());
    } catch ( ... ) {
        qCWarning(clientHandlerWorker, "发送消息时发生未知异常");
    }
}

void ClientHandlerWorker::disconnectClient() {
    qCInfo(clientHandlerWorker, "断开客户端连接: %s", qPrintable(clientId()));

    if ( m_socket ) {
        qCDebug(clientHandlerWorker) << "Socket state before disconnect:" << m_socket->state();
        m_socket->close();
        qCDebug(clientHandlerWorker) << "Socket state after close:" << m_socket->state();

        if ( m_socket->state() != QAbstractSocket::UnconnectedState ) {
            qCDebug(clientHandlerWorker) << "Waiting for disconnection...";
            if ( !m_socket->waitForDisconnected(5000) ) {
                qCWarning(clientHandlerWorker, "等待断开连接超时，强制关闭");
                m_socket->abort();
            }
        }
        qCDebug(clientHandlerWorker) << "Socket state final:" << m_socket->state();
    } else {
        qCWarning(clientHandlerWorker) << "Socket is null in disconnectClient()";
    }
}

void ClientHandlerWorker::forceDisconnect() {
    qCWarning(clientHandlerWorker, "强制断开客户端连接: %s", qPrintable(clientId()));

    if ( m_socket ) {
        m_socket->abort();
        // socket->abort() 会触发 disconnected 信号,
        // disconnected 信号会调用 onDisconnected(),
        // onDisconnected() 会发送 disconnected 信号给 ClientHandler,
        // ClientHandler 会调用 stop() 来停止 Worker
        qCDebug(clientHandlerWorker) << "Socket已abort,等待disconnected信号触发清理";
    } else {
        // 如果socket为空,直接发送disconnected信号（使用标志避免重复）
        if ( !m_disconnectSignalSent.exchange(true) ) {
            qCWarning(clientHandlerWorker) << "Socket为空,直接发送disconnected信号";
            emit disconnected();
        } else {
            qCWarning(clientHandlerWorker) << "Socket为空且disconnected信号已发送";
        }
    }
}

void ClientHandlerWorker::onReadyRead() {
    if ( !m_socket ) {
        return;
    }

    QByteArray newData = m_socket->readAll();
    if ( newData.isEmpty() ) {
        return;
    }

    {
        QMutexLocker locker(&m_statsMutex);
        m_bytesReceived += newData.size();
    }

    m_receiveBuffer.append(newData);

    // 处理缓冲区中的完整消息
    while ( m_receiveBuffer.size() >= static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) ) {
        // 尝试解析消息（包括解密）
        MessageHeader header;
        QByteArray payload;

        // 首先尝试解析当前缓冲区的数据
        if ( !Protocol::parseMessage(m_receiveBuffer, header, payload) ) {
            qCWarning(clientHandlerWorker, "消息解析失败");
            m_parseFailCount++;

            if ( m_parseFailCount > 10 ) {
                qCCritical(clientHandlerWorker, "连续解析失败次数过多，断开连接");
                forceDisconnect();
                return;
            }

            // 尝试重新同步
            m_receiveBuffer.remove(0, 1);
            continue;
        }

        // 重置解析失败计数
        m_parseFailCount = 0;

        // 计算消息的总大小（加密后的大小）
        // 关键修复：加密消息大小 = header大小 + 加密payload大小
        // 由于XOR加密长度不变，加密payload大小 = header.length
        qsizetype totalMessageSize = static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) + header.length;

        // 移除已处理的数据
        m_receiveBuffer.remove(0, totalMessageSize);

        // 处理消息
        processMessage(header, payload);
    }
}

void ClientHandlerWorker::onDisconnected() {
    qCInfo(clientHandlerWorker, "客户端断开连接: %s (连接时长: %lld 秒)",
        qPrintable(clientId()),
        m_connectionTime.secsTo(QDateTime::currentDateTime()));

    // 停止定时器
    if ( m_heartbeatCheckTimer ) {
        m_heartbeatCheckTimer->stop();
        qCDebug(clientHandlerWorker) << "心跳检查定时器已停止";
    }

    if ( m_heartbeatTimer ) {
        m_heartbeatTimer->stop();
        qCDebug(clientHandlerWorker) << "心跳定时器已停止";
    }

    // 记录连接统计信息
    qCDebug(clientHandlerWorker) << "连接统计 - 接收字节数:" << m_bytesReceived << "发送字节数:" << m_bytesSent;

    // 发送 disconnected 信号,让 ClientHandler 处理后续的停止逻辑
    // 注意:不要在这里调用 stop(),因为会导致信号还未处理完Worker就停止了
    // 使用成员变量确保只发送一次
    if ( !m_disconnectSignalSent.exchange(true) ) {
        qCCritical(clientHandlerWorker) << "!!!!! 准备发送 disconnected 信号给 ClientHandler !!!!!";
        qCCritical(clientHandlerWorker) << "Worker对象地址(this):" << this;
        qCCritical(clientHandlerWorker) << "signal发送线程:" << QThread::currentThread();
        qCCritical(clientHandlerWorker) << "Worker线程:" << thread();
        emit disconnected();
        qCCritical(clientHandlerWorker) << "!!!!! disconnected 信号已发出 !!!!!";

        // !!!!! 终极诊断:强制处理事件队列,看看信号是否在事件队列中 !!!!!
        qCCritical(clientHandlerWorker) << "!!!!! 强制处理事件队列 !!!!!";
        QCoreApplication::processEvents();
        QThread::msleep(10); // 给接收线程时间处理
        QCoreApplication::processEvents();
        qCCritical(clientHandlerWorker) << "!!!!! 事件队列处理完成 !!!!!";
    } else {
        qCDebug(clientHandlerWorker) << "disconnected 信号已发送过,跳过重复发送";
    }
}

void ClientHandlerWorker::onError(QAbstractSocket::SocketError error) {
    QString errorString = m_socket ? m_socket->errorString() : "未知错误";

    // 详细的错误日志记录
    qCWarning(clientHandlerWorker, "套接字错误 [%d]: %s (客户端: %s)",
        static_cast<int>(error), qPrintable(errorString), qPrintable(clientId()));

    // 根据错误类型进行分类处理
    bool shouldForceDisconnect = false;
    QString errorCategory;

    switch ( error ) {
        case QAbstractSocket::RemoteHostClosedError:
            errorCategory = "远程主机关闭连接";
            shouldForceDisconnect = true;
            break;
        case QAbstractSocket::NetworkError:
            errorCategory = "网络错误";
            shouldForceDisconnect = true;
            break;
        case QAbstractSocket::ConnectionRefusedError:
            errorCategory = "连接被拒绝";
            shouldForceDisconnect = true;
            break;
        case QAbstractSocket::HostNotFoundError:
            errorCategory = "主机未找到";
            shouldForceDisconnect = true;
            break;
        case QAbstractSocket::SocketTimeoutError:
            errorCategory = "套接字超时";
            shouldForceDisconnect = false; // 超时可能是临时的，不立即断开
            break;
        default:
            errorCategory = QString("其他错误 (%1)").arg(static_cast<int>(error));
            shouldForceDisconnect = false;
            break;
    }

    qCInfo(clientHandlerWorker, "错误分类: %s, 是否强制断开: %s",
        qPrintable(errorCategory), shouldForceDisconnect ? "是" : "否");

    // 发出错误信号
    emit errorOccurred(errorString);

    // 对于严重错误，强制断开连接
    if ( shouldForceDisconnect ) {
        qCWarning(clientHandlerWorker, "严重错误，强制断开客户端连接: %s", qPrintable(clientId()));
        forceDisconnect();
    }
}

void ClientHandlerWorker::sendHeartbeat() {
    // 目前服务器不主动发送心跳，只检查客户端心跳
}

void ClientHandlerWorker::checkHeartbeat() {
    QDateTime now = QDateTime::currentDateTime();
    if ( m_lastHeartbeat.msecsTo(now) > NetworkConstants::HEARTBEAT_TIMEOUT ) {
        qCWarning(clientHandlerWorker, "客户端心跳超时: %s", qPrintable(clientId()));
        forceDisconnect();
    }
}

void ClientHandlerWorker::processMessage(const MessageHeader& header, const QByteArray& payload) {
    switch ( header.type ) {
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
        default:
            qCWarning(clientHandlerWorker, "未知消息类型: %d", static_cast<int>(header.type));
            break;
    }
}

void ClientHandlerWorker::handleHandshakeRequest(const QByteArray& data) {
    Q_UNUSED(data)
        qCDebug(clientHandlerWorker) << "处理握手请求";
    sendHandshakeResponse();
}

void ClientHandlerWorker::handleAuthenticationRequest(const QByteArray& data) {
    qCDebug(clientHandlerWorker) << "处理认证请求";

    // 解析AuthenticationRequest结构体
    AuthenticationRequest authRequest;
    if ( !authRequest.decode(data) ) {
        qCWarning(clientHandlerWorker, "认证请求数据解析失败");
        sendAuthenticationResponse(AuthResult::INVALID_PASSWORD);
        return;
    }

    QString username = QString::fromUtf8(authRequest.username);
    QString passwordHash = QString::fromUtf8(authRequest.passwordHash);
    quint32 authMethod = authRequest.authMethod;

    qCDebug(clientHandlerWorker, "认证请求 - 用户名: %s, 认证方法: %u",
        qPrintable(username), authMethod);

    // 检查服务器是否设置了密码
    if ( m_expectedDigest.isEmpty() ) {
        // 服务器没有设置密码，允许任何用户直接认证成功
        qCDebug(clientHandlerWorker) << "服务器未设置密码，允许用户" << username << "直接认证成功";
        {
            QMutexLocker locker(&m_clientInfoMutex);
            m_isAuthenticated = true;
        }

        QString sessionId = generateSessionId();
        sendAuthenticationResponse(AuthResult::SUCCESS, sessionId);
        emit authenticated();
        qCInfo(clientHandlerWorker, "客户端认证成功: %s", qPrintable(clientId()));
        return;
    }

    // 检查认证方法
    if ( authMethod == 1 ) { // PBKDF2认证
        if ( passwordHash.isEmpty() ) {
            // 客户端请求挑战参数，发送AuthChallenge
            qCDebug(clientHandlerWorker, "发送PBKDF2挑战参数");
            sendAuthChallenge();
            return;
        } else {
            // 客户端发送了计算好的hash，验证它
            QByteArray clientDigest = QByteArray::fromHex(passwordHash.toUtf8());
            if ( clientDigest == m_expectedDigest ) {
                {
                    QMutexLocker locker(&m_clientInfoMutex);
                    m_isAuthenticated = true;
                }

                QString sessionId = generateSessionId();
                sendAuthenticationResponse(AuthResult::SUCCESS, sessionId);
                emit authenticated();
                qCInfo(clientHandlerWorker, "客户端认证成功: %s", qPrintable(clientId()));
            } else {
                m_failedAuthCount++;
                sendAuthenticationResponse(AuthResult::INVALID_PASSWORD);
                qCWarning(clientHandlerWorker, "客户端认证失败: %s (失败次数: %d)",
                    qPrintable(clientId()), m_failedAuthCount);

                if ( m_failedAuthCount >= 3 ) {
                    qCWarning(clientHandlerWorker, "认证失败次数过多，断开连接");
                    forceDisconnect();
                }
            }
        }
    } else {
        qCWarning(clientHandlerWorker, "不支持的认证方法: %u", authMethod);
        sendAuthenticationResponse(AuthResult::INVALID_PASSWORD);
    }
}

void ClientHandlerWorker::handleHeartbeat() {
    m_lastHeartbeat = QDateTime::currentDateTime();
    qCDebug(clientHandlerWorker) << "收到客户端心跳:" << clientId();
}

void ClientHandlerWorker::handleMouseEvent(const QByteArray& data) {
    if ( !isAuthenticated() ) {
        qCWarning(clientHandlerWorker, "未认证客户端尝试发送鼠标事件");
        return;
    }

    if ( !m_inputSimulator ) {
        qCWarning(clientHandlerWorker, "输入模拟器未初始化");
        return;
    }

    if ( data.size() < 13 ) { // 最小鼠标事件数据大小
        qCWarning(clientHandlerWorker, "鼠标事件数据不完整");
        return;
    }

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);

    qint32 x, y;
    quint8 buttons;
    qint32 wheelDelta;

    stream >> x >> y >> buttons >> wheelDelta;

    // 处理鼠标移动
    if ( x >= 0 && y >= 0 ) {
        m_inputSimulator->simulateMouseMove(x, y);
    }

    // 处理鼠标按键
    if ( buttons & 0x01 ) { // 左键
        m_inputSimulator->simulateMousePress(x, y, Qt::LeftButton);
    } else {
        m_inputSimulator->simulateMouseRelease(x, y, Qt::LeftButton);
    }

    if ( buttons & 0x02 ) { // 右键
        m_inputSimulator->simulateMousePress(x, y, Qt::RightButton);
    } else {
        m_inputSimulator->simulateMouseRelease(x, y, Qt::RightButton);
    }

    if ( buttons & 0x04 ) { // 中键
        m_inputSimulator->simulateMousePress(x, y, Qt::MiddleButton);
    } else {
        m_inputSimulator->simulateMouseRelease(x, y, Qt::MiddleButton);
    }

    // 处理滚轮
    if ( wheelDelta != 0 ) {
        m_inputSimulator->simulateMouseWheel(x, y, wheelDelta);
    }
}

void ClientHandlerWorker::handleKeyboardEvent(const QByteArray& data) {
    if ( !isAuthenticated() ) {
        qCWarning(clientHandlerWorker, "未认证客户端尝试发送键盘事件");
        return;
    }

    if ( !m_inputSimulator ) {
        qCWarning(clientHandlerWorker, "输入模拟器未初始化");
        return;
    }

    if ( data.size() < 9 ) { // 最小键盘事件数据大小
        qCWarning(clientHandlerWorker, "键盘事件数据不完整");
        return;
    }

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint32 key;
    quint32 modifiers;
    quint8 pressed;

    stream >> key >> modifiers >> pressed;

    Qt::Key qtKey = static_cast<Qt::Key>(key);
    Qt::KeyboardModifiers qtModifiers = static_cast<Qt::KeyboardModifiers>(modifiers);

    if ( pressed ) {
        m_inputSimulator->simulateKeyPress(qtKey, qtModifiers);
    } else {
        m_inputSimulator->simulateKeyRelease(qtKey, qtModifiers);
    }
}

void ClientHandlerWorker::sendHandshakeResponse() {
    HandshakeResponse response;
    response.serverVersion = PROTOCOL_VERSION;
    response.screenWidth = 1920; // 默认屏幕宽度
    response.screenHeight = 1080; // 默认屏幕高度
    response.colorDepth = 32; // 32位色深
    response.supportedFeatures = 0; // 可以根据需要设置服务器特性
    strcpy(response.serverName, "QtRemoteDesktop Server");
    strcpy(response.serverOS, "macOS");

    sendMessage(MessageType::HANDSHAKE_RESPONSE, response);
    qCDebug(clientHandlerWorker) << "发送握手响应";
}

void ClientHandlerWorker::sendAuthenticationResponse(AuthResult result, const QString& sessionId) {
    AuthenticationResponse response;
    response.result = result;
    strncpy(response.sessionId, sessionId.toUtf8().constData(), sizeof(response.sessionId) - 1);
    response.sessionId[sizeof(response.sessionId) - 1] = '\0'; // 确保字符串结束
    response.permissions = 0; // 默认权限

    sendMessage(MessageType::AUTHENTICATION_RESPONSE, response);
    qCDebug(clientHandlerWorker) << "发送认证响应，结果:" << static_cast<int>(result);
}

void ClientHandlerWorker::sendAuthChallenge() {
    AuthChallenge challenge;
    challenge.method = 1; // PBKDF2_SHA256
    challenge.iterations = 10000; // 与ServerWorker中的迭代次数保持一致
    challenge.keyLength = 32; // 与ServerWorker中的密钥长度保持一致

    // 使用服务器预设的盐值，如果没有则生成新的
    QByteArray salt = m_expectedSalt;
    if ( salt.isEmpty() ) {
        salt = QByteArray(16, 0); // 16字节盐值
        for ( int i = 0; i < salt.size(); ++i ) {
            salt[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
        }
        m_expectedSalt = salt;
    }

    // 将盐值转换为十六进制字符串
    QString saltHex = salt.toHex();
    strncpy(challenge.saltHex, saltHex.toUtf8().constData(), sizeof(challenge.saltHex) - 1);
    challenge.saltHex[sizeof(challenge.saltHex) - 1] = '\0';

    sendMessage(MessageType::AUTH_CHALLENGE, challenge);
    qCDebug(clientHandlerWorker, "发送认证挑战，方法: %u, 迭代次数: %u, 密钥长度: %u, 盐值: %s",
        challenge.method, challenge.iterations, challenge.keyLength, qPrintable(saltHex));
}

QString ClientHandlerWorker::generateSessionId() const {
    QByteArray data = QString("%1_%2_%3")
        .arg(clientId())
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(QRandomGenerator::global()->generate())
        .toUtf8();

    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}