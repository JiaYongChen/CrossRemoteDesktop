---
name: feedback-no-removal-comments
description: 删除/修改功能时不额外加「原为X/已移除」变更说明注释；正常的代码注释照常保留
metadata: 
  node_type: memory
  type: feedback
  originSessionId: a39f10bb-0f49-4681-9c2a-46fb7974a01b
---

**正常的代码注释（解释当前逻辑的非显然 WHY）照常保留、该写就写。** 本条只禁止一类注释：删除或修改既有功能时额外添加的**变更说明**——「这里原来是什么」「为什么移除」「已并入 X」。这类信息归 commit message，不进代码；删了就是删了，不为已消失的东西立纪念碑。

**Why:** 用户在合并 INVALID_USERNAME/PASSWORD 时明确反馈：给 Protocol.h 加的「0x01 原为 INVALID_USERNAME，已并入…」注释、文档里的「历史沿革」段落都属多余。变更原因归 commit message，不进代码。

**How to apply:** 删枚举值/方法/分支时直接删，编号该重排就重排（不留「空档」纪念注释）；不在代码里写「旧行为是…」「已移除…」。历史背景放 commit message。与 [[feedback-commit-style]] 配合。
