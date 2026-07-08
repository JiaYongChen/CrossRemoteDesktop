#pragma once

#include <QtCore/QtGlobal>

/// 结构化错误码枚举，按模块分类
enum class ErrorCode {
    Unknown = 0,

    // ─ 网络层 ─
    NetworkConnectionFailed,
    NetworkDisconnected,
    NetworkTimeout,
    NetworkTlsError,
    NetworkHeartbeatTimeout,

    // ─ 认证 ─
    AuthFailed,
    AuthChallengeFailed,
    AuthNotAuthenticated,

    // ─ 会话 ─
    SessionStartFailed,
    SessionAlreadyActive,
    SessionNotAuthenticated,

    // ─ 捕获 ─
    CaptureInitFailed,
    CaptureStartFailed,
    CaptureWorkerError,
    CaptureDxgiError,

    // ─ 数据处理 ─
    DataProcessingFailed,
    DataProcessingException,
    DecodeFailed,

    // ─ 队列 ─
    QueueOverflow,
    QueueEnqueueFailed,

    // ─ 线程 ─
    ThreadStartFailed,
    ThreadStateError,

    // ─ 服务端 ─
    ServerStartFailed,
    ServerBindFailed,
    TcpListenerError,

    // ─ 配置 ─
    ConfigInvalid,
    ConfigFileError,
};
