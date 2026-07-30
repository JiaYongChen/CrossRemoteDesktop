---
name: auth-challenge-removal-deferred
description: 移除 AUTH_CHALLENGE 的提议曾于 2026-07-29 被考虑并暂缓；用户有意未来重提
metadata: 
  node_type: memory
  type: project
  originSessionId: 13e752b5-92e8-439e-8bae-208214de5a18
---

2026-07-29（握手「只做认证不做协商」改造 brainstorm 期间）用户提议：去掉 AUTH_CHALLENGE，握手成功后服务端直接下发 PBKDF2 参数，客户端随后发 AUTHENTICATION_REQUEST。经权衡后用户决定「后面再做」——暂缓，未承诺实施，也未排期。

**Why（当时暂缓的理由）**：AUTH_CHALLENGE 本质是「异步 PBKDF2 派生出的每连接 salt 的投递载体」。把 salt 塞进握手响应会迫使握手响应等待慢速 PBKDF2 派生，认证总耗时不变（受限于派生就绪时刻），且无密码分支复杂化、重新耦合握手与认证。安全上中性（salt 仍每连接新鲜）。

**How to apply**：用户未来可能重提此精简。届时先复核上述权衡是否仍成立（尤其 PBKDF2 派生时机是否已改变），再决定是否纳入。应独立于握手 spec 单独成任务，勿与「握手 vs 协商」边界改造混在一起。
