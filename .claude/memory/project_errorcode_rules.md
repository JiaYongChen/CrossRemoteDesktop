---
name: project_errorcode_rules
description: ErrorCode 枚举只承载真正的系统故障，协议预期结果用语义化信号表达
metadata: 
  node_type: memory
  type: project
  originSessionId: 8fd9d8af-be74-4035-a28f-725a1324ee1b
  modified: 2026-08-05T05:04:31.366Z
---

# ErrorCode 枚举规范

## 核心规则

`ErrorCode` 枚举中**只能包含真正的系统故障**——即非预期发生的、需要诊断和处理的基础设施/运行时错误。

## 禁止放入 ErrorCode 的值

协议的**预期拒绝结果**不得放入 ErrorCode，这些应使用独立的语义化信号：

- 版本不兼容 → `versionMismatched()` 信号
- 认证凭据无效 → `authenticationFailed()` 信号
- 认证访问被拒 → `authenticationFailed()` 信号
- 未认证操作 → 不需错误码（守卫条件变为死代码后直接移除）

## 当前合法值（12 个）

网络层：`NetworkConnectionFailed`, `NetworkDisconnected`, `NetworkTlsError`, `NetworkHeartbeatTimeout`
协议：`DecodeFailed`
捕获：`CaptureInitFailed`, `CaptureStartFailed`, `CaptureWorkerError`
数据处理：`DataProcessingException`
队列：`QueueOverflow`
线程：`ThreadStartFailed`
服务端：`ServerStartFailed`, `TcpListenerError`

**Why:** 将协议预期结果和系统故障混在同一个枚举中，强迫消费者 `switch(error.code)` 从中筛选——这正是扁平枚举思维在错误码层的延续。区分后 `errorOccurred` 语义纯粹："出事了，需要诊断"。

**How to apply:** 新增错误码时先问"这是系统故障还是协议预期结果？"协议预期结果不添加 ErrorCode 值，使用独立信号。
