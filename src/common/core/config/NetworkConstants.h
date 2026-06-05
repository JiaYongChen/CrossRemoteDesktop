#pragma once

/**
 * @file NetworkConstants.h
 * @brief 网络相关常量的统一定义
 */

 /// 网络连接相关常量定义
namespace NetworkConstants {
    // ==================== 连接和超时设置 ====================
    const int DEFAULT_CONNECTION_TIMEOUT = 15000;  // 15秒 - 连接建立超时时间
    const int HEARTBEAT_TIMEOUT = 25000;           // 25秒 - 心跳超时（无心跳后断开）
    const int HEARTBEAT_INTERVAL = 15000;          // 15秒 - 心跳发送间隔
    const int DEFAULT_RECONNECT_INTERVAL = 3000;   // 3秒 - 重连间隔

    // ==================== 缓冲区大小 ====================
    const int SOCKET_SEND_BUFFER_SIZE = 262144;    // 256KB - TCP发送缓冲区
    const int SOCKET_RECEIVE_BUFFER_SIZE = 262144; // 256KB - TCP接收缓冲区

    // ==================== 数据包大小限制 ====================
    const int MAX_PACKET_SIZE = 50 * 1024 * 1024;  // 50MB - 支持大分辨率传输

    // ==================== 网络性能优化参数 ====================
    const int TCP_NODELAY_ENABLED = 1;             // 启用TCP_NODELAY减少延迟
    const int KEEP_ALIVE_ENABLED = 1;              // 启用TCP Keep-Alive
}
