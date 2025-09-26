#ifndef NETWORKCONSTANTS_H
#define NETWORKCONSTANTS_H

// 网络连接相关常量定义
namespace NetworkConstants {
    // 连接和超时设置
    const int DEFAULT_CONNECTION_TIMEOUT = 15000;  // 15秒 (增加连接超时)
    const int HEARTBEAT_TIMEOUT = 45000;           // 45秒 (增加心跳超时)
    const int HEARTBEAT_INTERVAL = 15000;          // 15秒 (增加心跳间隔)
    // PING_INTERVAL 已移除 - 统计功能不再使用
    const int DEFAULT_RECONNECT_INTERVAL = 3000;   // 3秒 (减少重连间隔)
    const int ADAPTIVE_HEARTBEAT_MIN = 5000;       // 5秒 (自适应心跳最小间隔)
    const int ADAPTIVE_HEARTBEAT_MAX = 30000;      // 30秒 (自适应心跳最大间隔)
    
    // 缓冲区大小
    const int NETWORK_BUFFER_SIZE = 262144;        // 256KB - 大幅增大缓冲区提高传输效率
    const int SOCKET_SEND_BUFFER_SIZE = 262144;    // 256KB - TCP发送缓冲区
    const int SOCKET_RECEIVE_BUFFER_SIZE = 262144; // 256KB - TCP接收缓冲区
    const int MAX_PACKET_SIZE = 5 * 1024 * 1024;   // 5MB - 支持更大的数据包
    
    // 网络性能优化参数
    const int TCP_NODELAY_ENABLED = 1;             // 启用TCP_NODELAY减少延迟
    const int KEEP_ALIVE_ENABLED = 1;              // 启用TCP Keep-Alive
    const int KEEP_ALIVE_IDLE = 7200;              // Keep-Alive空闲时间(秒)
    const int KEEP_ALIVE_INTERVAL = 75;            // Keep-Alive探测间隔(秒)
    const int KEEP_ALIVE_COUNT = 9;                // Keep-Alive探测次数
    
    // 重试设置
    const int MAX_RETRY_COUNT = 3;
    const int RETRY_DELAY = 1000;                  // 1秒
    
    // 端口范围
    const int MIN_PORT = 1024;
    const int MAX_PORT = 65535;
    const int DEFAULT_PORT = 5921;
}

#endif // NETWORKCONSTANTS_H