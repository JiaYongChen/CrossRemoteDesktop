---
name: 不使用功能分支
description: 用户偏好直接在 master 上提交，不创建功能分支
metadata:
  node_type: memory
  type: feedback
---

直接在 master 上做修改和提交，不要创建新分支。

**Why:** 用户明确表示不需要 feature 分支工作流。

**How to apply:** 所有代码修改、commit、push 都在 master 分支上完成，不新建分支。
