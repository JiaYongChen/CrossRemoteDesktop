#pragma once

/// 结构化错误码枚举，按模块分类
enum class ErrorCode {
    Unknown = 0,

    // ─ 网络层 ─
    NetworkConnectionFailed,
    NetworkDisconnected,
    NetworkTlsError,
    NetworkHeartbeatTimeout,

    // ─ 认证 ─
    AuthInvalidCredentials, ///< 凭据无效（用户名/密码统一，可重试）
    AuthAccessDenied,       ///< 尝试次数过多，拒绝访问（终态）

    // ─ 会话 ─
    SessionNotAuthenticated,

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
