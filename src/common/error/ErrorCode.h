#pragma once

/// 结构化错误码枚举，按模块分类（纯系统故障）
enum class ErrorCode {
    Unknown = 0,

    // ─ 网络层 ─
    NetworkConnectionFailed,
    NetworkDisconnected,
    NetworkTlsError,
    NetworkHeartbeatTimeout,

    // ─ 捕获 ─
    CaptureInitFailed,
    CaptureStartFailed,
    CaptureWorkerError,

    // ─ 数据处理 ─
    DataProcessingException,
    DecodeFailed,

    // ─ 队列 ─
    QueueOverflow,

    // ─ 线程 ─
    ThreadStartFailed,

    // ─ 服务端 ─
    ServerStartFailed,
    TcpListenerError,
};
