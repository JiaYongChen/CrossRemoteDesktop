---
name: auth-challenge-removal-done
description: AUTH_CHALLENGE 已于 2026-07-30 移除（协议 v3）：认证参数并入 HANDSHAKE_RESPONSE + 惰性 PBKDF2 派生 + 消息编号重排
metadata:
  node_type: memory
  type: project
  originSessionId: 13e752b5-92e8-439e-8bae-208214de5a18
---

2026-07-30 已实施（用户确认，承接 2026-07-29 的暂缓提议）：AUTH_CHALLENGE 消息**彻底移除**（枚举项/结构体/编解码/两端处理全部删除，标识符 challenge→auth 统一命名）。协议 v3：
- PBKDF2 认证参数（salt/iterations/keyLength）随 HANDSHAKE_RESPONSE 携带；saltHex 为空 = 无密码模式（客户端等待服务端直通认证）。
- 连接与认证消息重排编号：HANDSHAKE_REQUEST=0x0001、HANDSHAKE_RESPONSE=0x0002、AUTHENTICATION_REQUEST=0x0003、AUTHENTICATION_RESPONSE=0x0004、SESSION_CAPABILITIES=0x0005。
- 服务端改为**惰性派生**：ServerSession 收到 worker 的 handshakeRequestReceived 信号才生成 salt + PBKDF2 派生，握手响应等待配置会合后发送（会合点：ClientHandlerWorker::deliverHandshakeResponse）。

**Why（当年暂缓的理由已不成立）**：原顾虑是"salt 进握手响应会迫使响应等待预派生"。改为惰性派生后，未握手的连接（端口扫描等）零派生成本，缓解按连接预派生 ~1.7s 的 CPU 放大（审查发现 3 的一部分）；客户端少一轮消息往返。总耗时不变但结构与命名更简。

**How to apply**：协议版本为 3（ProtocolConstants::ProtocolVersion），客户端与服务端同仓库发布、无旧端兼容包袱；后续认证改动基于此结构。相关：[[auth-retry-model]]。
