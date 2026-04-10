#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ClientHandlerWorker.h"
#include "../simulator/InputSimulator.h"
#include "../dataflow/QueueManager.h"
#include "../dataflow/DataFlowStructures.h"

// 取消Windows SDK定义的事件宏,避免与MessageType冲突
#ifdef MOUSE_EVENT
#undef MOUSE_EVENT
#endif
#ifdef KEYBOARD_EVENT
#undef KEYBOARD_EVENT
#endif

#include "../../common/core/network/Protocol.h"
#include "../../common/core/config/NetworkConstants.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QSslConfiguration>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDataStream>
#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QRandomGenerator>
#include <QtConcurrent/QtConcurrent>
#include <cstring>


ClientHandlerWorker::ClientHandlerWorker(qintptr socketDescriptor,
                                         const QSslCertificate& certificate,
                                         const QSslKey& privateKey,
                                         QObject* parent)
    : Worker(parent)
    , m_socketDescriptor(socketDescriptor)
    , m_socket(nullptr)
    , m_sslCertificate(certificate)
    , m_sslPrivateKey(privateKey)
    , m_clientPort(0)
    , m_isAuthenticated(false)
    , m_failedAuthCount(0)
    , m_connectionTime(QDateTime::currentDateTime())
    , m_lastHeartbeat(QDateTime::currentDateTime())
    , m_heartbeatSendTimer(nullptr)
    , m_heartbeatCheckTimer(nullptr)
    , m_cursorUpdateTimer(nullptr)
    , m_bytesReceived(0)
    , m_bytesSent(0)
    , m_inputSimulator(nullptr)
    , m_queueManager(nullptr) {
    qCDebug(lcClientHandlerWorker) << "ClientHandlerWorker 构造函数调用，套接字描述符:" << socketDescriptor;
    setName("ClientHandlerWorker");
}

ClientHandlerWorker::~ClientHandlerWorker() {
    qCDebug(lcClientHandlerWorker) << "ClientHandlerWorker 析构函数";

    qCDebug(lcClientHandlerWorker) << "ClientHandlerWorker 析构完成";
}

bool ClientHandlerWorker::initialize() {
    qCInfo(lcClientHandlerWorker) << "初始化 ClientHandlerWorker";

    // 在Worker线程中创建SSL socket
    m_socket = new QSslSocket(this);

    // 使用套接字描述符初始化socket
    if ( !m_socket->setSocketDescriptor(m_socketDescriptor) ) {
        qCCritical(lcClientHandlerWorker) << "无法设置套接字描述符:" << m_socket->errorString();
        delete m_socket;
        m_socket = nullptr;
        return false;
    }

    // 配置TLS证书和密钥
    if ( !m_sslCertificate.isNull() && !m_sslPrivateKey.isNull() ) {
        m_socket->setLocalCertificate(m_sslCertificate);
        m_socket->setPrivateKey(m_sslPrivateKey);
        QSslConfiguration sslConfig = m_socket->sslConfiguration();
        sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
        m_socket->setSslConfiguration(sslConfig);
    }

    // 设置TCP优化选项
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, NetworkConstants::KEEP_ALIVE_ENABLED);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, NetworkConstants::TCP_NODELAY_ENABLED);
    m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, NetworkConstants::SOCKET_SEND_BUFFER_SIZE);
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, NetworkConstants::SOCKET_RECEIVE_BUFFER_SIZE);

    // 获取客户端信息
    {
        QMutexLocker locker(&m_clientInfoMutex);
        m_clientAddress = m_socket->peerAddress().toString();
        m_clientPort = m_socket->peerPort();
        m_clientId = QString("%1:%2").arg(m_clientAddress).arg(m_clientPort);
    }

    // Mark connected (atomic flag for thread-safe cross-thread queries)
    m_isConnectedAtomic.store(true, std::memory_order_release);

    // 连接套接字信号
    connect(m_socket, &QSslSocket::readyRead, this, &ClientHandlerWorker::onReadyRead);
    connect(m_socket, &QSslSocket::disconnected, this, &ClientHandlerWorker::onDisconnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
        this, &ClientHandlerWorker::onError);
    connect(m_socket, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
        this, [this](const QList<QSslError>& errors) {
        // Build list of acceptable self-signed certificate errors.
        // Only these specific errors are tolerated; all others are treated as fatal.
        QList<QSslError> expectedErrors;
        for ( const QSslError& error : errors ) {
            switch ( error.error() ) {
                case QSslError::SelfSignedCertificate:
                case QSslError::SelfSignedCertificateInChain:
                case QSslError::HostNameMismatch:
                    // Tolerable for self-signed server certificates
                    qCDebug(lcClientHandlerWorker) << "Ignoring expected SSL error:" << error.errorString();
                    expectedErrors.append(error);
                    break;
                default:
                    qCWarning(lcClientHandlerWorker) << "SSL error:" << error.errorString();
                    break;
            }
        }
        if ( !expectedErrors.isEmpty() ) {
            m_socket->ignoreSslErrors(expectedErrors);
        }
    });

    // 启动服务端TLS握手
    m_socket->startServerEncryption();

    // 创建心跳检查定时器
    m_heartbeatCheckTimer = new QTimer(this);
    m_heartbeatCheckTimer->setInterval(NetworkConstants::HEARTBEAT_TIMEOUT);
    connect(m_heartbeatCheckTimer, &QTimer::timeout, this, &ClientHandlerWorker::checkHeartbeat);

    // 创建心跳发送定时器
    m_heartbeatSendTimer = new QTimer(this);
    m_heartbeatSendTimer->setInterval(NetworkConstants::HEARTBEAT_INTERVAL);
    connect(m_heartbeatSendTimer, &QTimer::timeout, this, &ClientHandlerWorker::sendHeartbeat);

    // 创建光标类型更新定时器 (60 FPS)
    m_cursorUpdateTimer = new QTimer(this);
    m_cursorUpdateTimer->setInterval(10);
    connect(m_cursorUpdateTimer, &QTimer::timeout, this, &ClientHandlerWorker::sendCursorType);

    // 创建输入模拟器
    m_inputSimulator = new InputSimulator(this);
    if ( !m_inputSimulator->initialize() ) {
        qCWarning(lcClientHandlerWorker) << "输入模拟器初始化失败，客户端:" << clientId();
    }

    // 获取队列管理器
    m_queueManager = QueueManager::instance();
    if ( !m_queueManager ) {
        qCWarning(lcClientHandlerWorker) << "无法获取队列管理器实例";
    }

    // 启动心跳检查定时器
    m_heartbeatCheckTimer->start();

    // 启动心跳发送定时器
    m_heartbeatSendTimer->start();

    qCInfo(lcClientHandlerWorker) << "ClientHandlerWorker 初始化成功，客户端:" << clientId();

    return true;
}

void ClientHandlerWorker::cleanup() {
    qCInfo(lcClientHandlerWorker) << "清理 ClientHandlerWorker 资源";

    // 在工作线程中停止并显式删除定时器子对象。
    // 根本原因修复：这些子 QObject 是在工作线程的 initialize() 中以 this 为 parent 创建的，
    // 其线程亲缘性属于工作线程。若只停止不删除，Worker 析构时 QObject::~QObject() 会从
    // 主线程调用 deleteChildren()，触发跨线程的 QSocketNotifier/QTimer 清理，
    // 进而导致 QCoreApplication::sendEvent 的跨线程致命断言。
    // 在此（doStop() 调用链，仍在工作线程内）显式删除，确保主线程析构 Worker 时
    // children 列表已为空，彻底消除跨线程删除子 QObject 的问题。
    if ( m_heartbeatCheckTimer ) {
        m_heartbeatCheckTimer->stop();
        delete m_heartbeatCheckTimer;
        m_heartbeatCheckTimer = nullptr;
    }

    if ( m_heartbeatSendTimer ) {
        m_heartbeatSendTimer->stop();
        delete m_heartbeatSendTimer;
        m_heartbeatSendTimer = nullptr;
    }

    if ( m_cursorUpdateTimer ) {
        m_cursorUpdateTimer->stop();
        delete m_cursorUpdateTimer;
        m_cursorUpdateTimer = nullptr;
    }

    // 在工作线程中显式删除输入模拟器
    if ( m_inputSimulator ) {
        delete m_inputSimulator;
        m_inputSimulator = nullptr;
    }

    // 在工作线程中断开并显式删除套接字。
    // QSslSocket 内部的 QSocketNotifier 清理（unregisterSocketNotifier）在错误线程中
    // 会调用 sendEvent，必须在工作线程内完成删除。
    if ( m_socket ) {
        m_socket->disconnectFromHost();
        if ( m_socket->state() != QAbstractSocket::UnconnectedState ) {
            m_socket->waitForDisconnected(3000);
        }
        delete m_socket;
        m_socket = nullptr;
    }

    qCInfo(lcClientHandlerWorker) << "ClientHandlerWorker 资源清理完成";
}

void ClientHandlerWorker::processTask() {
    // 在Worker线程中，主要的处理逻辑通过信号槽机制触发
    // 这里处理周期性任务：连接状态检查、数据接收、屏幕数据发送

    // 检查连接状态
    if ( m_socket && m_socket->state() != QAbstractSocket::ConnectedState ) {
        // 不要直接调用stop(),而是通过disconnected信号让ClientHandler来停止
        // 使用成员变量确保只触发一次
        if ( !m_disconnectSignalSent.exchange(true) ) {
            qCDebug(lcClientHandlerWorker) << "检测到连接断开(processTask)，触发disconnected信号";
            emit disconnected();
        }
        return;
    }

    // 认证成功后，异步从处理队列获取并发送屏幕数据
    // Guard flag prevents event queue accumulation: only post if no pending invocation
    if ( isAuthenticated() && m_queueManager && !m_sendScreenDataPending.exchange(true) ) {
        QMetaObject::invokeMethod(this, "sendScreenDataFromQueue", Qt::QueuedConnection);
    }

    // Hint to workLoop: if we posted a send, there's likely work to do — skip idle sleep.
    setDidWork(isAuthenticated() && m_queueManager != nullptr);
}

void ClientHandlerWorker::sendScreenDataFromQueue() {
    // Reset the guard flag so processTask can post the next invocation
    m_sendScreenDataPending.store(false);

    // Fix 1: 在出队之前先检查 socket 连接状态和认证状态。
    // 若 socket 已断开，dequeueProcessedData() 会静默消耗队列数据却无法发送，
    // 造成数据丢失并给调用方留下"仍在传输"的假象。
    if ( !m_socket || m_socket->state() != QAbstractSocket::ConnectedState ) {
        return;
    }

    if ( !m_queueManager || !isAuthenticated() ) {
        return;
    }

    // Batch send: dequeue and send up to MAX_SEND_BATCH frames per invocation.
    // This reduces the overhead of workLoop's per-iteration msleep and
    // QMetaObject::invokeMethod round-trip when frames are queued up.
    static constexpr int MAX_SEND_BATCH = 3;
    int sent = 0;

    while ( sent < MAX_SEND_BATCH ) {
        // Re-check connection before each send in the batch
        if ( !m_socket || m_socket->state() != QAbstractSocket::ConnectedState ) {
            break;
        }

        ProcessedData processedData;
        if ( !m_queueManager->dequeueProcessedData(processedData) ) {
            break; // Queue empty
        }

        // 验证数据有效性
        if ( !processedData.isValid() ) {
            qCWarning(lcClientHandlerWorker) << "ProcessedData无效，跳过发送，帧ID:" << processedData.originalFrameId;
            continue;
        }

        // 创建ScreenData消息
        ScreenData screenData;
        screenData.x = 0;
        screenData.y = 0;
        screenData.imageData = processedData.compressedData;
        screenData.width = processedData.imageSize.width();
        screenData.height = processedData.imageSize.height();
        screenData.originalWidth = processedData.originalImageSize.width();
        screenData.originalHeight = processedData.originalImageSize.height();
        screenData.dataSize = processedData.compressedData.size();

        // 设置压缩标志位
        quint8 flags = static_cast<quint8>(ScreenDataFlags::NONE);
        if ( processedData.isZstdCompressed ) {
            flags |= static_cast<quint8>(ScreenDataFlags::ZSTD_COMPRESSED);
        }
        if ( processedData.isScaled ) {
            flags |= static_cast<quint8>(ScreenDataFlags::SCALED);
        }
        screenData.flags = flags;

        // 预先编码消息,然后发送
        QByteArray messageData = Protocol::createMessage(MessageType::SCREEN_DATA, screenData);

        if ( messageData.isEmpty() ) {
            qCWarning(lcClientHandlerWorker) << "消息编码失败，messageData为空";
            continue;
        }

        sendEncodedMessage(messageData);
        ++sent;
    }
}

void ClientHandlerWorker::sendCursorType() {
    // 检查连接和认证状态
    if ( !m_socket || !m_socket->isOpen() ) {
        return;
    }

    if ( !isAuthenticated() ) {
        return;
    }

    if ( !m_inputSimulator ) {
        return;
    }

    // 获取当前光标类型
    int cursorType = m_inputSimulator->getCurrentCursorType();

    // 创建光标类型消息（仅包含类型）
    CursorMessage message(static_cast<Qt::CursorShape>(cursorType));

    // 发送光标类型消息
    QByteArray messageData = Protocol::createMessage(MessageType::CURSOR_POSITION, message);
    if ( !messageData.isEmpty() ) {
        sendEncodedMessage(messageData);
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
    // Use atomic flag for thread safety — QSslSocket::state() is not thread-safe
    return m_isConnectedAtomic.load(std::memory_order_acquire);
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
    // Guard with mutex: this Q_INVOKABLE may be called from the main thread
    // while the worker thread reads these fields during authentication.
    QMutexLocker locker(&m_clientInfoMutex);
    m_expectedSalt = salt;
    m_expectedDigest = digest;
}

void ClientHandlerWorker::setPbkdf2Params(quint32 iterations, quint32 keyLength) {
    QMutexLocker locker(&m_clientInfoMutex);
    m_pbkdf2Iterations = iterations;
    m_pbkdf2KeyLength = keyLength;
}

void ClientHandlerWorker::sendMessage(MessageType type, const IMessageCodec& message) {
    try {
        // 使用Protocol::createMessage来创建加密的消息
        QByteArray messageData = Protocol::createMessage(type, message);

        if ( messageData.isEmpty() ) {
            qCWarning(lcClientHandlerWorker) << "消息数据为空，跳过发送";
            return;
        }

        // 调用统一的发送实现
        sendEncodedMessage(messageData);

        // 只在非屏幕数据消息时记录详细日志，避免高频日志输出
        if ( type != MessageType::SCREEN_DATA ) {
            qCDebug(lcClientHandlerWorker) << "消息发送完成: 类型=" << static_cast<int>(type)
                << ", 大小=" << messageData.size() << "bytes";
        }

    } catch ( const std::exception& e ) {
        qCWarning(lcClientHandlerWorker) << "发送消息时发生异常:" << e.what();
    } catch ( ... ) {
        qCWarning(lcClientHandlerWorker) << "发送消息时发生未知异常";
    }
}

void ClientHandlerWorker::sendEncodedMessage(const QByteArray& messageData) {
    if ( !m_socket || m_socket->state() != QAbstractSocket::ConnectedState ) {
        qCWarning(lcClientHandlerWorker) << "套接字未连接，无法发送消息";
        return;
    }

    if ( messageData.isEmpty() ) {
        qCWarning(lcClientHandlerWorker) << "消息数据为空，跳过发送";
        return;
    }

    try {
        // 直接发送完整消息，让TCP层处理分段
        // 注意：协议层的加密消息是一个完整单元，不能在应用层分块
        // TCP会自动处理大消息的分段和重组
        qint64 totalSize = messageData.size();
        qint64 bytesWritten = m_socket->write(messageData);

        if ( bytesWritten == -1 ) {
            qCWarning(lcClientHandlerWorker) << "发送消息失败:" << m_socket->errorString();
            return;
        }

        if ( bytesWritten != totalSize ) {
            qCWarning(lcClientHandlerWorker) << "消息部分发送: 期望" << totalSize << "bytes，实际" << bytesWritten << "bytes";
        }

        // 更新统计信息（按写入的字节数，不是消息大小）
        if ( bytesWritten > 0 ) {
            QMutexLocker locker(&m_statsMutex);
            m_bytesSent += bytesWritten;
        }

        // 数据大小和发送数据大小日志
        // qCDebug(lcClientHandlerWorker) << "数据大小:" << totalSize << "bytes" << "发送数据大小:" << bytesWritten << "bytes";

    } catch ( const std::exception& e ) {
        qCWarning(lcClientHandlerWorker) << "发送消息时发生异常:" << e.what();
    } catch ( ... ) {
        qCWarning(lcClientHandlerWorker) << "发送消息时发生未知异常";
    }
}

void ClientHandlerWorker::disconnectClient() {
    qCInfo(lcClientHandlerWorker) << "断开客户端连接:" << clientId();

    if ( m_socket ) {
        qCDebug(lcClientHandlerWorker) << "Socket state before disconnect:" << m_socket->state();
        m_socket->close();
        qCDebug(lcClientHandlerWorker) << "Socket state after close:" << m_socket->state();

        if ( m_socket->state() != QAbstractSocket::UnconnectedState ) {
            qCDebug(lcClientHandlerWorker) << "Waiting for disconnection...";
            if ( !m_socket->waitForDisconnected(5000) ) {
                qCWarning(lcClientHandlerWorker) << "等待断开连接超时，强制关闭";
                m_socket->abort();
            }
        }
        qCDebug(lcClientHandlerWorker) << "Socket state final:" << m_socket->state();
    } else {
        qCWarning(lcClientHandlerWorker) << "Socket is null in disconnectClient()";
    }
}

void ClientHandlerWorker::forceDisconnect() {
    m_isConnectedAtomic.store(false, std::memory_order_release);
    qCWarning(lcClientHandlerWorker) << "强制断开客户端连接:" << clientId();

    m_receiveBuffer.clear();

    if ( m_socket ) {
        m_socket->abort();
        qCDebug(lcClientHandlerWorker) << "Socket已abort,等待disconnected信号触发清理";
    } else {
        // 如果socket为空,直接发送disconnected信号（使用标志避免重复）
        if ( !m_disconnectSignalSent.exchange(true) ) {
            qCWarning(lcClientHandlerWorker) << "Socket为空,直接发送disconnected信号";
            emit disconnected();
        } else {
            qCWarning(lcClientHandlerWorker) << "Socket为空且disconnected信号已发送";
        }
    }
}

void ClientHandlerWorker::onReadyRead() {
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
        qCCritical(lcClientHandlerWorker) << "接收缓冲区超过最大限制:" << NetworkConstants::MAX_PACKET_SIZE
            << "当前大小:" << m_receiveBuffer.size()
            << "新增数据:" << newData.size();
        forceDisconnect();
        return;
    }

    // 预留空间以减少内存分配次数
    m_receiveBuffer.reserve(m_receiveBuffer.size() + newData.size());
    m_receiveBuffer.append(newData);

    // 更新心跳时间
    m_lastHeartbeat = QDateTime::currentDateTime();

    {
        QMutexLocker locker(&m_statsMutex);
        m_bytesReceived += newData.size();
    }

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

void ClientHandlerWorker::onDisconnected() {
    m_isConnectedAtomic.store(false, std::memory_order_release);
    qCInfo(lcClientHandlerWorker) << "客户端断开连接:" << clientId()
        << "(连接时长:" << m_connectionTime.secsTo(QDateTime::currentDateTime()) << "秒)";

    // 停止定时器
    if ( m_heartbeatCheckTimer ) {
        m_heartbeatCheckTimer->stop();
        qCDebug(lcClientHandlerWorker) << "心跳检查定时器已停止";
    }

    if ( m_heartbeatSendTimer ) {
        m_heartbeatSendTimer->stop();
        qCDebug(lcClientHandlerWorker) << "心跳发送定时器已停止";
    }

    // 记录连接统计信息
    qCDebug(lcClientHandlerWorker) << "连接统计 - 接收字节数:" << m_bytesReceived << "发送字节数:" << m_bytesSent;

    // 发送 disconnected 信号,让 ClientHandler 处理后续的停止逻辑
    // 注意:不要在这里调用 stop(),因为会导致信号还未处理完Worker就停止了
    // 使用成员变量确保只发送一次
    if ( !m_disconnectSignalSent.exchange(true) ) {
        qCCritical(lcClientHandlerWorker) << "!!!!! 准备发送 disconnected 信号给 ClientHandler !!!!!";
        qCCritical(lcClientHandlerWorker) << "Worker对象地址(this):" << this;
        qCCritical(lcClientHandlerWorker) << "signal发送线程:" << QThread::currentThread();
        qCCritical(lcClientHandlerWorker) << "Worker线程:" << thread();
        emit disconnected();
        qCCritical(lcClientHandlerWorker) << "!!!!! disconnected 信号已发出 !!!!!";

        // !!!!! 终极诊断:强制处理事件队列,看看信号是否在事件队列中 !!!!!
        qCCritical(lcClientHandlerWorker) << "!!!!! 强制处理事件队列 !!!!!";
        QCoreApplication::processEvents();
        QThread::msleep(10); // 给接收线程时间处理
        QCoreApplication::processEvents();
        qCCritical(lcClientHandlerWorker) << "!!!!! 事件队列处理完成 !!!!!";
    } else {
        qCDebug(lcClientHandlerWorker) << "disconnected 信号已发送过,跳过重复发送";
    }
}

void ClientHandlerWorker::onError(QAbstractSocket::SocketError error) {
    QString errorString = m_socket ? m_socket->errorString() : "未知错误";

    // 详细的错误日志记录
    qCWarning(lcClientHandlerWorker) << "套接字错误 [" << static_cast<int>(error) << "]:"
        << errorString << "(客户端:" << clientId() << ")";

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

    qCInfo(lcClientHandlerWorker) << "错误分类:" << errorCategory
        << ", 是否强制断开:" << (shouldForceDisconnect ? "是" : "否");

    // 发出错误信号
    emit errorOccurred(errorString);

    // 对于严重错误，强制断开连接
    if ( shouldForceDisconnect ) {
        qCWarning(lcClientHandlerWorker) << "严重错误，强制断开客户端连接:" << clientId();
        forceDisconnect();
    }
}

void ClientHandlerWorker::checkHeartbeat() {
    QDateTime now = QDateTime::currentDateTime();
    if ( m_lastHeartbeat.msecsTo(now) > NetworkConstants::HEARTBEAT_TIMEOUT ) {
        qCWarning(lcClientHandlerWorker) << "客户端心跳超时:" << clientId();
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
        case MessageType::HEARTBEAT_RESPONSE:
            handleHeartbeat();
            break;
        case MessageType::MOUSE_EVENT:
            handleMouseEvent(payload);
            break;
        case MessageType::KEYBOARD_EVENT:
            handleKeyboardEvent(payload);
            break;
        case MessageType::CLIPBOARD_DATA:
            handleClipboardData(payload);
            break;
        default:
            qCWarning(lcClientHandlerWorker) << "未知消息类型:" << static_cast<int>(header.type);
            break;
    }
}

void ClientHandlerWorker::handleHandshakeRequest(const QByteArray& data) {
    Q_UNUSED(data)
        qCDebug(lcClientHandlerWorker) << "处理握手请求";
    sendHandshakeResponse();
}

void ClientHandlerWorker::handleAuthenticationRequest(const QByteArray& data) {
    qCDebug(lcClientHandlerWorker) << "处理认证请求";

    // Rate limiting: if within backoff period from last failure, reject immediately
    if ( m_failedAuthCount > 0 && m_lastFailedAuthTime.isValid() ) {
        int requiredDelayMs = std::min(
            AUTH_BASE_DELAY_MS * (1 << (m_failedAuthCount - 1)),
            AUTH_MAX_DELAY_MS);
        qint64 elapsedMs = m_lastFailedAuthTime.msecsTo(QDateTime::currentDateTime());
        if ( elapsedMs < requiredDelayMs ) {
            qCWarning(lcClientHandlerWorker) << "认证速率限制: 距上次失败仅" << elapsedMs
                << "ms (需等待" << requiredDelayMs << "ms), 拒绝请求:" << clientId();
            // Don't send response during backoff — silent drop to slow down brute force
            return;
        }
    }

    // 解析AuthenticationRequest结构体
    AuthenticationRequest authRequest;
    if ( !authRequest.decode(data) ) {
        qCWarning(lcClientHandlerWorker) << "认证请求数据解析失败";
        sendAuthenticationResponse(AuthResult::INVALID_PASSWORD);
        return;
    }

    QString username = authRequest.username;
    QString passwordHash = authRequest.passwordHash;
    quint32 authMethod = authRequest.authMethod;

    qCDebug(lcClientHandlerWorker) << "认证请求 - 用户名:" << username << ", 认证方法:" << authMethod;

    // 检查服务器是否设置了密码
    // Take a snapshot under lock to avoid data race with setExpectedPasswordDigest()
    QByteArray expectedDigest;
    {
        QMutexLocker locker(&m_clientInfoMutex);
        expectedDigest = m_expectedDigest;
    }
    if ( expectedDigest.isEmpty() ) {
        // 服务器没有设置密码，允许任何用户直接认证成功
        qCDebug(lcClientHandlerWorker) << "服务器未设置密码，允许用户" << username << "直接认证成功";
        {
            QMutexLocker locker(&m_clientInfoMutex);
            m_isAuthenticated = true;
        }

        QString sessionId = generateSessionId();
        sendAuthenticationResponse(AuthResult::SUCCESS, sessionId);

        // 启动光标位置更新定时器
        if ( m_cursorUpdateTimer ) {
            m_cursorUpdateTimer->start();
        }

        emit authenticated();
        qCInfo(lcClientHandlerWorker) << "客户端认证成功: " << clientId();
        return;
    }

    // 检查认证方法
    if ( authMethod == 1 ) { // PBKDF2认证
        if ( passwordHash.isEmpty() ) {
            // 客户端请求挑战参数，发送AuthChallenge
            qCDebug(lcClientHandlerWorker) << "发送PBKDF2挑战参数";
            sendAuthChallenge();
            return;
        } else {
            // 客户端发送了计算好的hash，验证它
            QByteArray clientDigest = QByteArray::fromHex(passwordHash.toUtf8());
            if ( clientDigest == expectedDigest ) {
                {
                    QMutexLocker locker(&m_clientInfoMutex);
                    m_isAuthenticated = true;
                }

                QString sessionId = generateSessionId();
                sendAuthenticationResponse(AuthResult::SUCCESS, sessionId);

                // 启动光标位置更新定时器
                if ( m_cursorUpdateTimer ) {
                    m_cursorUpdateTimer->start();
                }

                emit authenticated();
                qCInfo(lcClientHandlerWorker) << "客户端认证成功: " << clientId();
            } else {
                m_failedAuthCount++;
                m_lastFailedAuthTime = QDateTime::currentDateTime();
                qCWarning(lcClientHandlerWorker) << "客户端认证失败:" << clientId()
                    << "(失败次数:" << m_failedAuthCount << "/" << MAX_AUTH_FAILURES << ")";

                if ( m_failedAuthCount >= MAX_AUTH_FAILURES ) {
                    qCWarning(lcClientHandlerWorker) << "认证失败次数达到上限，断开连接:" << clientId();
                    sendAuthenticationResponse(AuthResult::ACCESS_DENIED);
                    forceDisconnect();
                } else {
                    // Exponential backoff: delay = base * 2^(failures-1), capped at max
                    int delayMs = std::min(
                        AUTH_BASE_DELAY_MS * (1 << (m_failedAuthCount - 1)),
                        AUTH_MAX_DELAY_MS);
                    qCInfo(lcClientHandlerWorker) << "认证速率限制: 延迟" << delayMs << "ms 后发送响应";
                    QTimer::singleShot(delayMs, this, [this]() {
                        sendAuthenticationResponse(AuthResult::INVALID_PASSWORD);
                    });
                }
            }
        }
    } else {
        qCWarning(lcClientHandlerWorker) << "不支持的认证方法: " << authMethod;
        sendAuthenticationResponse(AuthResult::INVALID_PASSWORD);
    }
}

void ClientHandlerWorker::handleHeartbeat() {
    // 收到客户端的心跳响应，更新最后心跳时间
    m_lastHeartbeat = QDateTime::currentDateTime();
    qCDebug(lcClientHandlerWorker) << "收到客户端心跳响应:" << clientId();
}

void ClientHandlerWorker::sendHeartbeat() {
    if ( !m_socket || !m_socket->isOpen() ) {
        qCDebug(lcClientHandlerWorker) << "套接字未连接，无法发送心跳请求";
        return;
    }

    if ( !isAuthenticated() ) {
        qCDebug(lcClientHandlerWorker) << "客户端未认证，跳过心跳发送";
        return;
    }

    sendMessage(MessageType::HEARTBEAT, BaseMessage());

    qCDebug(lcClientHandlerWorker) << "发送心跳请求到客户端:" << clientId();
}

void ClientHandlerWorker::handleMouseEvent(const QByteArray& data) {
    if ( !isAuthenticated() ) {
        qCWarning(lcClientHandlerWorker) << "未认证客户端尝试发送鼠标事件";
        return;
    }

    if ( !m_inputSimulator ) {
        qCWarning(lcClientHandlerWorker) << "输入模拟器未初始化";
        return;
    }

    // MouseEvent结构: eventType(1) + x(2) + y(2) + wheelDelta(2) = 7字节
    if ( data.size() < 7 ) {
        qCWarning(lcClientHandlerWorker) << "鼠标事件数据不完整，期望至少7字节，实际: " << data.size();
        return;
    }

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint8 eventType;
    qint16 x, y;
    qint16 wheelDelta;

    stream >> eventType >> x >> y >> wheelDelta;

    // 检查数据流状态
    if ( stream.status() != QDataStream::Ok ) {
        qCWarning(lcClientHandlerWorker) << "鼠标事件数据解析失败";
        return;
    }

    // 根据 eventType 处理不同的鼠标事件
    MouseEventType mouseEventType = static_cast<MouseEventType>(eventType);

    switch ( mouseEventType ) {
        case MouseEventType::MOVE:
            // 处理鼠标移动
            m_inputSimulator->simulateMouseMove(x, y);
            break;
        case MouseEventType::LEFT_PRESS:
            m_inputSimulator->simulateMousePress(x, y, Qt::LeftButton);
            break;
        case MouseEventType::LEFT_RELEASE:
            m_inputSimulator->simulateMouseRelease(x, y, Qt::LeftButton);
            break;
        case MouseEventType::RIGHT_PRESS:
            m_inputSimulator->simulateMousePress(x, y, Qt::RightButton);
            break;
        case MouseEventType::RIGHT_RELEASE:
            m_inputSimulator->simulateMouseRelease(x, y, Qt::RightButton);
            break;
        case MouseEventType::MIDDLE_PRESS:
            m_inputSimulator->simulateMousePress(x, y, Qt::MiddleButton);
            break;
        case MouseEventType::MIDDLE_RELEASE:
            m_inputSimulator->simulateMouseRelease(x, y, Qt::MiddleButton);
            break;
        case MouseEventType::LEFT_DOUBLE_CLICK:
            m_inputSimulator->simulateMouseDoubleClick(x, y, Qt::LeftButton);
            break;
        case MouseEventType::RIGHT_DOUBLE_CLICK:
            m_inputSimulator->simulateMouseDoubleClick(x, y, Qt::RightButton);
            break;
        case MouseEventType::MIDDLE_DOUBLE_CLICK:
            m_inputSimulator->simulateMouseDoubleClick(x, y, Qt::MiddleButton);
            break;
        case MouseEventType::WHEEL_UP:
        case MouseEventType::WHEEL_DOWN:
            // 处理滚轮事件
            if ( wheelDelta != 0 ) {
                m_inputSimulator->simulateMouseWheel(x, y, wheelDelta);
            }
            break;
        default:
            qCWarning(lcClientHandlerWorker) << "未知的鼠标事件类型: " << static_cast<int>(eventType);
            break;
    }
}

void ClientHandlerWorker::handleKeyboardEvent(const QByteArray& data) {
    if ( !isAuthenticated() ) {
        qCWarning(lcClientHandlerWorker) << "未认证客户端尝试发送键盘事件";
        return;
    }

    if ( !m_inputSimulator ) {
        qCWarning(lcClientHandlerWorker) << "输入模拟器未初始化";
        return;
    }

    // KeyboardEvent结构: eventType(1) + keyCode(4) + modifiers(4) + text(8) = 17字节
    if ( data.size() < 17 ) {
        qCWarning(lcClientHandlerWorker) << "键盘事件数据不完整，期望至少17字节，实际: " << data.size();
        return;
    }

    // 使用 KeyboardEvent 的 decode 方法解析
    KeyboardEvent keyEvent;
    if ( !keyEvent.decode(data) ) {
        qCWarning(lcClientHandlerWorker) << "键盘事件数据解析失败";
        return;
    }

    qCDebug(lcClientHandlerWorker) << "键盘事件: eventType=" << static_cast<int>(keyEvent.eventType)
        << "keyCode=" << keyEvent.keyCode << "modifiers=" << keyEvent.modifiers
        << "text=" << keyEvent.text;

    // Qt 键盘事件的 key() 和 modifiers() 是分离的
    // key() 返回纯键码（不包含 KeypadModifier）
    // modifiers() 返回修饰符标志（包含 KeypadModifier: 0x20000000）
    // 我们需要组合它们以便正确识别小键盘按键

    int qtKey = static_cast<int>(keyEvent.keyCode);
    Qt::KeyboardModifiers qtModifiers = static_cast<Qt::KeyboardModifiers>(keyEvent.modifiers);

    // 如果 modifiers 包含 KeypadModifier，将其添加到 key 值中
    if ( qtModifiers & Qt::KeypadModifier ) {
        qtKey |= 0x20000000;  // 添加 KeypadModifier 标志
        qCDebug(lcClientHandlerWorker) << "Keypad modifier detected, combined key:" << Qt::hex << qtKey;
    }

    if ( keyEvent.eventType == KeyboardEventType::KEY_PRESS ) {
        m_inputSimulator->simulateKeyPress(qtKey, qtModifiers);
    } else if ( keyEvent.eventType == KeyboardEventType::KEY_RELEASE ) {
        m_inputSimulator->simulateKeyRelease(qtKey, qtModifiers);
    } else {
        qCWarning(lcClientHandlerWorker) << "未知的键盘事件类型: " << static_cast<int>(keyEvent.eventType);
    }
}

void ClientHandlerWorker::sendHandshakeResponse() {
    HandshakeResponse response;
    response.serverVersion = PROTOCOL_VERSION;
    response.screenWidth = 1920; // 默认屏幕宽度
    response.screenHeight = 1080; // 默认屏幕高度
    response.colorDepth = 32; // 32位色深
    response.supportedFeatures = 0; // 可以根据需要设置服务器特性
    response.serverName = QStringLiteral("QtRemoteDesktop Server");
#ifdef Q_OS_WIN
    response.serverOS = QStringLiteral("Windows");
#elif defined(Q_OS_MACOS)
    response.serverOS = QStringLiteral("macOS");
#else
    response.serverOS = QStringLiteral("Linux");
#endif

    sendMessage(MessageType::HANDSHAKE_RESPONSE, response);
    qCDebug(lcClientHandlerWorker) << "发送握手响应";
}

void ClientHandlerWorker::sendAuthenticationResponse(AuthResult result, const QString& sessionId) {
    AuthenticationResponse response;
    response.result = result;
    response.sessionId = sessionId;
    response.permissions = 0; // 默认权限

    sendMessage(MessageType::AUTHENTICATION_RESPONSE, response);
    qCDebug(lcClientHandlerWorker) << "发送认证响应，结果:" << static_cast<int>(result);
}

void ClientHandlerWorker::sendAuthChallenge() {
    AuthChallenge challenge;
    challenge.method = 1; // PBKDF2_SHA256

    // Read PBKDF2 params and salt under lock (may be set from main thread)
    QByteArray salt;
    {
        QMutexLocker locker(&m_clientInfoMutex);
        challenge.iterations = m_pbkdf2Iterations;
        challenge.keyLength = m_pbkdf2KeyLength;
        salt = m_expectedSalt;
    }

    // 使用服务器预设的盐值，如果没有则生成新的
    if ( salt.isEmpty() ) {
        salt = QByteArray(16, 0); // 16字节盐值
        for ( int i = 0; i < salt.size(); ++i ) {
            salt[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
        }
        QMutexLocker locker(&m_clientInfoMutex);
        m_expectedSalt = salt;
    }

    // 将盐值转换为十六进制字符串
    challenge.saltHex = QString::fromLatin1(salt.toHex());

    sendMessage(MessageType::AUTH_CHALLENGE, challenge);
    qCDebug(lcClientHandlerWorker) << "发送认证挑战，方法:" << challenge.method
        << ", 迭代次数:" << challenge.iterations << ", 密钥长度:" << challenge.keyLength
        << ", 盐值:" << challenge.saltHex;
}

QString ClientHandlerWorker::generateSessionId() const {
    QByteArray data = QString("%1_%2_%3")
        .arg(clientId())
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(QRandomGenerator::global()->generate())
        .toUtf8();

    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

// ==================== 剪贴板消息处理 ====================

void ClientHandlerWorker::handleClipboardData(const QByteArray& data) {
    ClipboardMessage message;
    if ( !message.decode(data) ) {
        qCWarning(lcClientHandlerWorker) << "剪贴板消息解析失败";
        return;
    }

    if ( message.isText() ) {
        qCDebug(lcClientHandlerWorker) << "接收到剪贴板文本，长度: " << message.text().length();

        // 更新服务器端剪贴板
        emit clipboardTextReceived(message.text());

        // 广播到其他客户端（通过 ServerManager）
        emit broadcastClipboardText(message.text());
    } else if ( message.isImage() ) {
        qCDebug(lcClientHandlerWorker) << "接收到剪贴板图片，尺寸:" << message.width << "x" << message.height
            << ", 数据大小:" << message.imageData().size();

        // 更新服务器端剪贴板
        emit clipboardImageReceived(message.imageData());

        // 广播到其他客户端（通过 ServerManager）
        emit broadcastClipboardImage(message.imageData(), message.width, message.height);
    }
}