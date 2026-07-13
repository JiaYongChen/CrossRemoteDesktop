---
name: feedback_no_docs_in_git
description: 不要将 docs/ 目录下的文件添加到 git 追踪中
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 3f34e9d7-5d54-4f80-b864-0cd6300d02cd
---

docs/ 目录已在 .gitignore 中排除。设计文档、计划文档等仅供本地参考，不应纳入版本管理。任何时候都不要使用 `git add -f docs/` 或其他方式将 docs/ 下的文件加入 git。

**Why:** docs/ 下的设计文档和实现计划是开发过程的中间产物，不应提交到仓库中。之前曾因 `git add -f docs/` 将 4 个文档误提交，后已通过 `git rm --cached` 移除。在 .gitignore 中已配置 `docs/` 规则阻止自动追踪。

**How to apply:** 写入新设计文档到 docs/ 后，无需提交该文件。如果需要版本化文档，与用户先确认替代位置。
