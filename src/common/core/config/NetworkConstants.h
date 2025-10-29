#ifndef NETWORKCONSTANTS_H
#define NETWORKCONSTANTS_H

/**
 * @file NetworkConstants.h
 * @brief 网络相关常量的统一定义
 *
 * 本文件定义了所有网络传输相关的常量，包括连接、超时、缓冲区、心跳等。
 * 项目中的其他网络相关代码应该引用此文件，避免重复定义。
 */

 // 网络连接相关常量定义
namespace NetworkConstants {
    // ==================== 连接和超时设置 ====================
    const int DEFAULT_CONNECTION_TIMEOUT = 15000;  // 15秒 - 连接建立超时时间
    const int HEARTBEAT_TIMEOUT = 45000;           // 45秒 - 心跳超时（无心跳后断开）
    const int HEARTBEAT_INTERVAL = 15000;          // 15秒 - 心跳发送间隔
    const int DEFAULT_RECONNECT_INTERVAL = 3000;   // 3秒 - 重连间隔
    const int ADAPTIVE_HEARTBEAT_MIN = 5000;       // 5秒 - 自适应心跳最小间隔
    const int ADAPTIVE_HEARTBEAT_MAX = 30000;      // 30秒 - 自适应心跳最大间隔
    const int CONNECTION_TIMEOUT = 30000;          // 30秒 - 通用连接超时

    // ==================== 缓冲区大小 ====================
    const int SOCKET_SEND_BUFFER_SIZE = 262144;    // 256KB - TCP发送缓冲区
    const int SOCKET_RECEIVE_BUFFER_SIZE = 262144; // 256KB - TCP接收缓冲区
    const int SOCKET_BUFFER_SIZE = 64 * 1024;      // 64KB - 通用Socket缓冲区大小

    // ==================== 数据包大小限制 ====================
    const int MAX_PACKET_SIZE = 50 * 1024 * 1024;  // 50MB - 与屏幕数据上限保持一致，支持大分辨率传输

    // ==================== 网络性能优化参数 ====================
    const int TCP_NODELAY_ENABLED = 1;             // 启用TCP_NODELAY减少延迟（禁用Nagle算法）
    const int KEEP_ALIVE_ENABLED = 1;              // 启用TCP Keep-Alive
    const int KEEP_ALIVE_IDLE = 7200;              // Keep-Alive空闲时间(秒)
    const int KEEP_ALIVE_INTERVAL = 75;            // Keep-Alive探测间隔(秒)
    const int KEEP_ALIVE_COUNT = 9;                // Keep-Alive探测次数
    const int MAX_FRAMES_PER_CYCLE = 2;            // 每个周期最多发送的帧数

    // ==================== 重试设置 ====================
    const int MAX_RETRY_COUNT = 3;                 // 最大重试次数
    const int MAX_RECONNECT_ATTEMPTS = 5;          // 最大重连尝试次数
    const int RETRY_DELAY = 1000;                  // 1秒 - 重试延迟
    const int RECONNECT_DELAY_MS = 1000;           // 1秒 - 重连延迟

    // ==================== 端口范围和默认端口 ====================
    const int MIN_PORT = 1024;                     // 最小端口号（避免使用系统保留端口）
    const int MAX_PORT = 65535;                    // 最大端口号
    const int DEFAULT_PORT = 5921;                 // 默认服务端口

    // ==================== 连接数限制 ====================
    const int MAX_CONNECTIONS = 100;               // 最大同时连接数
}

#endif // NETWORKCONSTANTS_H