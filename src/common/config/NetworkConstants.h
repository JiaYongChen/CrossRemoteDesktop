#pragma once

/**
 * @file NetworkConstants.h
 * @brief 网络相关常量的统一定义
 */

 /// 网络连接相关常量定义
namespace NetworkConstants {
    // ==================== 连接和超时设置 ====================
    inline constexpr int DefaultConnectionTimeout = 15000;  // 15秒 - 连接建立超时时间
    inline constexpr int HeartbeatTimeout = 25000;          // 25秒 - 心跳超时（无心跳后断开）
    inline constexpr int HeartbeatInterval = 15000;         // 15秒 - 心跳发送间隔
    inline constexpr int DefaultReconnectInterval = 3000;   // 3秒 - 重连间隔

    // ==================== 缓冲区大小 ====================
    inline constexpr int SocketSendBufferSize = 262144;     // 256KB - TCP发送缓冲区
    inline constexpr int SocketReceiveBufferSize = 262144;  // 256KB - TCP接收缓冲区

    // ==================== 数据包大小限制 ====================
    inline constexpr int MaxPacketSize = 50 * 1024 * 1024;  // 50MB - 支持大分辨率传输

    // ==================== 网络性能优化参数 ====================
    inline constexpr int TcpNoDelayEnabled = 1;             // 启用TCP_NODELAY减少延迟
    inline constexpr int KeepAliveEnabled = 1;              // 启用TCP Keep-Alive

    // ==================== 端口和重连 ====================
    inline constexpr int DefaultServerPort = 5921;

    inline constexpr int DefaultMaxReconnectAttempts = 5;
}
