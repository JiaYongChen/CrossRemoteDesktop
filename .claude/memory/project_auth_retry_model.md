---
name: project-auth-retry-model
description: 认证采用连接内重试模型（2026-07-30 用户确认）：失败不断连、失败响应统一、全失败计数、阶梯锁定复活
metadata: 
  node_type: memory
  type: project
  originSessionId: fd86a409-e216-4725-a28c-a6a15749f8fd
---

认证流程采用**连接内重试模型**（2026-07-30 用户拍板，提交 7769b4f/ee71507）：
- 认证失败**不断连**（仅 ACCESS_DENIED 终局断连），客户端弹凭据对话框重新输入后，复用缓存的认证 salt 在**同连接**重派生重发（免重连 TLS + 服务端 PBKDF2 成本）。
- 任何失败（用户名/空哈希/摘要）统一回通用 INVALID_PASSWORD（服务端不再发 INVALID_USERNAME，枚举值仅为协议兼容保留），真实原因只入日志——消除用户名枚举 oracle。
- 全部失败计入计数，MaxAuthFailures=5 阶梯锁定与指数退避恢复设计意图（旧"失败即断连"策略曾使二者成为死代码）。
- 退避公式单实现 `AuthHandler::backoffDelayMs()`（指数预钳位消除 int UB），isRateLimited 与 worker 共用。

**Why:** 旧策略下每连接计数随断连归零（限速形同虚设）、失败响应的码/时序差异构成枚举 oracle、合法用户输错密码要重付 ~1.7s 派生成本；且阶梯锁定机制本就为连接内重试设计，策略与机制矛盾。

**How to apply:** 改动认证相关代码时维护不变式「失败不断连 + 失败响应统一 + 全失败计数」；延时失败响应定时器必须复检 m_isAuthenticated。遗留审查发现中：3/4（按对端地址的全局限速与连接上限）仍是开放项且与本模型互补（连接内重试只抬高单连接成本，不阻重连攻击）；8（畸形包即时响应的时序差异）为已接受的低危残留；相关决策见 [[auth-challenge-removal-done]]（挑战消息移除，认证参数并入握手响应）。
