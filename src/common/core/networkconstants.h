#ifndef NETWORKCONSTANTS_H
#define NETWORKCONSTANTS_H

// 网络连接相关常量定义
namespace NetworkConstants {
    // 连接超时设置
    const int DEFAULT_CONNECTION_TIMEOUT = 10000;  // 10秒
    const int HEARTBEAT_TIMEOUT = 30000;           // 30秒
    const int HEARTBEAT_INTERVAL = 10000;          // 10秒 - 减少心跳间隔提高响应性
    // PING_INTERVAL 已移除 - 统计功能不再使用
    const int DEFAULT_RECONNECT_INTERVAL = 5000;   // 5秒
    
    // 缓冲区大小
    const int NETWORK_BUFFER_SIZE = 65536;         // 64KB - 增大缓冲区提高传输效率
    const int MAX_PACKET_SIZE = 5 * 1024 * 1024;   // 5MB - 支持更大的数据包
    
    // 重试设置
    const int MAX_RETRY_COUNT = 3;
    const int RETRY_DELAY = 1000;                  // 1秒
    
    // 端口范围
    const int MIN_PORT = 1024;
    const int MAX_PORT = 65535;
    const int DEFAULT_PORT = 5921;
}

#endif // NETWORKCONSTANTS_H