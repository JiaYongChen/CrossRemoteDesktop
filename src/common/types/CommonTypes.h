#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QSize>
#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtCore/QMetaType>

/**
 * @file CommonTypes.h
 * @brief 通用类型定义
 *
 * 定义在server和client之间共享的通用数据类型和结构体。
 * 这些类型用于数据传输、状态管理和配置等场景。
 */

namespace QtRemoteDesktop {

    /**
     * @brief 连接状态枚举
     */
    enum class ConnectionState {
        Disconnected = 0,    ///< 未连接
        Connecting = 1,      ///< 连接中
        Connected = 2,       ///< 已连接
        Authenticating = 3,  ///< 认证中
        Authenticated = 4,   ///< 已认证
        Error = 5           ///< 错误状态
    };

    /**
     * @brief 会话状态枚举
     */
    enum class SessionState {
        Inactive = 0,       ///< 非活动
        Starting = 1,       ///< 启动中
        Active = 2,         ///< 活动中
        Paused = 3,         ///< 暂停
        Stopping = 4,       ///< 停止中
        Error = 5          ///< 错误状态
    };

    /**
     * @brief 数据质量级别
     */
    enum class QualityLevel {
        Low = 0,           ///< 低质量
        Medium = 1,        ///< 中等质量
        High = 2,          ///< 高质量
        Lossless = 3       ///< 无损质量
    };

    /**
     * @brief 错误级别
     */
    enum class ErrorLevel {
        Info = 0,          ///< 信息
        Warning = 1,       ///< 警告
        Error = 2,         ///< 错误
        Critical = 3       ///< 严重错误
    };

    /**
     * @brief 性能统计信息
     */
    struct PerformanceStats {
        quint32 frameCount = 0;        ///< 帧数量
        double currentFPS = 0.0;       ///< 当前FPS
        quint64 bytesReceived = 0;     ///< 接收字节数
        quint64 bytesSent = 0;         ///< 发送字节数
        quint32 latencyMs = 0;         ///< 延迟(毫秒)
        QDateTime sessionStartTime;    ///< 会话开始时间

        /**
         * @brief 重置统计信息
         */
        void reset() {
            frameCount = 0;
            currentFPS = 0.0;
            bytesReceived = 0;
            bytesSent = 0;
            latencyMs = 0;
            sessionStartTime = QDateTime::currentDateTime();
        }
    };

    /**
     * @brief 连接信息
     */
    struct ConnectionInfo {
        QString connectionId;          ///< 连接ID
        QString hostAddress;           ///< 主机地址
        quint16 port = 0;             ///< 端口号
        ConnectionState state = ConnectionState::Disconnected; ///< 连接状态
        QDateTime connectTime;         ///< 连接时间
        QString clientName;            ///< 客户端名称
        QString serverName;            ///< 服务器名称

        /**
         * @brief 检查连接是否有效
         */
        bool isValid() const {
            return !connectionId.isEmpty() && !hostAddress.isEmpty() && port > 0;
        }
    };

    /**
     * @brief 屏幕信息
     */
    struct ScreenInfo {
        QSize resolution;              ///< 分辨率
        qint32 colorDepth = 32;       ///< 颜色深度
        qreal refreshRate = 60.0;     ///< 刷新率
        QString name;                  ///< 屏幕名称
        QRect geometry;                ///< 屏幕几何信息
        bool isPrimary = false;        ///< 是否为主屏幕

        /**
         * @brief 检查屏幕信息是否有效
         */
        bool isValid() const {
            return resolution.isValid() && colorDepth > 0 && refreshRate > 0;
        }
    };

} // namespace QtRemoteDesktop

// 注册元类型以支持信号槽传递
Q_DECLARE_METATYPE(QtRemoteDesktop::ConnectionState)
Q_DECLARE_METATYPE(QtRemoteDesktop::SessionState)
Q_DECLARE_METATYPE(QtRemoteDesktop::QualityLevel)
Q_DECLARE_METATYPE(QtRemoteDesktop::ErrorLevel)
Q_DECLARE_METATYPE(QtRemoteDesktop::PerformanceStats)
Q_DECLARE_METATYPE(QtRemoteDesktop::ConnectionInfo)
Q_DECLARE_METATYPE(QtRemoteDesktop::ScreenInfo)