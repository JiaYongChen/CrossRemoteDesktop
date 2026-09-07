---
name: 文档版本管理范围
description: 正式项目文档正常纳入 git，仅 docs/superpowers/ 中的临时产物保持忽略
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 3f34e9d7-5d54-4f80-b864-0cd6300d02cd
---

`docs/` 不是整体忽略目录。`docs/ARCHITECTURE.md`、`docs/BUILD.md`、`docs/CHANGELOG.md` 和 `docs/PLATFORM_SUPPORT.md` 均为正式项目文档，应随对应实现更新并正常纳入版本管理。

`.gitignore` 当前只忽略 `docs/superpowers/`。该目录及 `.superpowers/` 用于本地流程产生的临时设计/计划材料，不应使用 `git add -f` 强制纳入版本控制。

**How to apply:** 修改正式文档时与代码一起审查、验证并提交；新建文档前判断它是长期项目资料还是临时工作产物，后者放入已忽略的 `docs/superpowers/`。
