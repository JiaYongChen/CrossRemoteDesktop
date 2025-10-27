#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QMetaType>
#include <QtNetwork/QAbstractSocket>

/**
 * @file NetworkTypes.h
 * @brief 网络相关类型定义
 *
 * 定义网络通信中使用的数据类型、错误码和状态信息。
 */

namespace QtRemoteDesktop {
    namespace Network {

        /**
         * @brief 网络错误类型
         */
        enum class ErrorType {
            NoError = 0,                    ///< 无错误
            ConnectionRefused = 1,          ///< 连接被拒绝
            RemoteHostClosed = 2,           ///< 远程主机关闭连接
            HostNotFound = 3,               ///< 主机未找到
            SocketTimeout = 4,              ///< 套接字超时
            NetworkError = 5,               ///< 网络错误
            SslHandshakeError = 6,          ///< SSL握手错误
            AuthenticationError = 7,        ///< 认证错误
            ProtocolError = 8,              ///< 协议错误
            DataCorruption = 9,             ///< 数据损坏
            UnknownError = 99               ///< 未知错误
        };

        /**
         * @brief 网络连接类型
         */
        enum class ConnectionType {
            TCP = 0,                        ///< TCP连接
            UDP = 1,                        ///< UDP连接
            WebSocket = 2,                  ///< WebSocket连接
            SSL = 3                         ///< SSL/TLS连接
        };

        /**
         * @brief 数据传输模式
         */
        enum class TransferMode {
            Blocking = 0,                   ///< 阻塞模式
            NonBlocking = 1,                ///< 非阻塞模式
            Asynchronous = 2                ///< 异步模式
        };

        /**
         * @brief 网络错误信息
         */
        struct ErrorInfo {
            ErrorType type = ErrorType::NoError;  ///< 错误类型
            QString message;                       ///< 错误消息
            qint32 code = 0;                      ///< 错误代码
            QDateTime timestamp;                   ///< 错误时间
            QString details;                       ///< 详细信息

            /**
             * @brief 检查是否有错误
             */
            bool hasError() const {
                return type != ErrorType::NoError;
            }

            /**
             * @brief 清除错误信息
             */
            void clear() {
                type = ErrorType::NoError;
                message.clear();
                code = 0;
                timestamp = QDateTime();
                details.clear();
            }
        };

        /**
         * @brief 网络统计信息
         */
        struct Statistics {
            quint64 bytesReceived = 0;             ///< 接收字节数
            quint64 bytesSent = 0;                 ///< 发送字节数
            quint32 packetsReceived = 0;           ///< 接收包数
            quint32 packetsSent = 0;               ///< 发送包数
            quint32 packetsLost = 0;               ///< 丢失包数
            quint32 averageLatencyMs = 0;          ///< 平均延迟(毫秒)
            quint32 maxLatencyMs = 0;              ///< 最大延迟(毫秒)
            quint32 minLatencyMs = 0;              ///< 最小延迟(毫秒)
            double throughputKbps = 0.0;           ///< 吞吐量(Kbps)
            QDateTime sessionStartTime;            ///< 会话开始时间

            /**
             * @brief 重置统计信息
             */
            void reset() {
                bytesReceived = 0;
                bytesSent = 0;
                packetsReceived = 0;
                packetsSent = 0;
                packetsLost = 0;
                averageLatencyMs = 0;
                maxLatencyMs = 0;
                minLatencyMs = 0;
                throughputKbps = 0.0;
                sessionStartTime = QDateTime::currentDateTime();
            }

            /**
             * @brief 计算丢包率
             */
            double packetLossRate() const {
                if ( packetsSent == 0 ) return 0.0;
                return static_cast<double>(packetsLost) / packetsSent * 100.0;
            }
        };

        /**
         * @brief 连接配置
         */
        struct ConnectionConfig {
            QString hostAddress;                   ///< 主机地址
            quint16 port = 0;                     ///< 端口号
            ConnectionType type = ConnectionType::TCP; ///< 连接类型
            TransferMode mode = TransferMode::NonBlocking; ///< 传输模式
            qint32 timeoutMs = 30000;             ///< 超时时间(毫秒)
            qint32 retryCount = 3;                ///< 重试次数
            qint32 bufferSize = 65536;            ///< 缓冲区大小
            bool enableEncryption = false;         ///< 启用加密
            QString username;                      ///< 用户名
            QString password;                      ///< 密码

            /**
             * @brief 检查配置是否有效
             */
            bool isValid() const {
                return !hostAddress.isEmpty() && port > 0 && timeoutMs > 0;
            }
        };

        /**
         * @brief 将Qt套接字错误转换为网络错误类型
         */
        inline ErrorType fromSocketError(QAbstractSocket::SocketError error) {
            switch ( error ) {
                case QAbstractSocket::ConnectionRefusedError:
                    return ErrorType::ConnectionRefused;
                case QAbstractSocket::RemoteHostClosedError:
                    return ErrorType::RemoteHostClosed;
                case QAbstractSocket::HostNotFoundError:
                    return ErrorType::HostNotFound;
                case QAbstractSocket::SocketTimeoutError:
                    return ErrorType::SocketTimeout;
                case QAbstractSocket::NetworkError:
                    return ErrorType::NetworkError;
                case QAbstractSocket::SslHandshakeFailedError:
                    return ErrorType::SslHandshakeError;
                default:
                    return ErrorType::UnknownError;
            }
        }

    } // namespace Network
} // namespace QtRemoteDesktop

// 注册元类型以支持信号槽传递
Q_DECLARE_METATYPE(QtRemoteDesktop::Network::ErrorType)
Q_DECLARE_METATYPE(QtRemoteDesktop::Network::ConnectionType)
Q_DECLARE_METATYPE(QtRemoteDesktop::Network::TransferMode)
Q_DECLARE_METATYPE(QtRemoteDesktop::Network::ErrorInfo)
Q_DECLARE_METATYPE(QtRemoteDesktop::Network::Statistics)
Q_DECLARE_METATYPE(QtRemoteDesktop::Network::ConnectionConfig)