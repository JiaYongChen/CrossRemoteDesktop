---
name: feedback_windows_git_case
description: Windows 上 git 跟踪路径大小写与磁盘不符会导致改动提交不掉+永久幻影 M，用 git mv 纠正
metadata: 
  node_type: memory
  type: feedback
  originSessionId: a63f58be-19a6-4f15-b49f-2ba7a550835e
---

在 Windows（文件系统不区分大小写、git `core.ignorecase=true`）上，若某文件被 git 以**与磁盘不一致的大小写**跟踪（如索引里 `settingsdialog.ui`、磁盘上 `SettingsDialog.ui`），对该文件的修改会陷入"改了但提交不掉"的泥潭：`git status` 永久显示 ` M`、`git diff` 可能误导性地显示 0 行、`git checkout -- <磁盘大小写路径>` 报 pathspec 不匹配、改动看似从未真正入库。

**Why:** Windows 把两种大小写当同一物理文件，但 git 记录的路径名按索引里的（错误）大小写。Edit 改的是磁盘文件，`git add` 时路径错配产生诡异行为——改动可能未真正进入提交（曾发生 SettingsDialog.ui 汉化一直留在工作区未入 HEAD）。

**How to apply:**
- 提交后某文件反复显示 ` M` 且 `git diff` 行为反常时，**先用 `git ls-files | Select-String <文件名>` 核对 git 实际跟踪的大小写**，别先怀疑 CRLF。
- 根治：`git mv <索引里的大小写> <磁盘正确大小写>` 纠正跟踪路径，再提交。
- 验证改动是否入库用 `git show HEAD:<path>` 检查内容，而非只看 `git diff` 为空。
- 大小写错配常与行尾（CRLF/LF）问题混淆——本项目已加 `.gitattributes`（`* text=auto`）规范化行尾；若仍有幻影 M，优先查大小写。

相关：[[project_directory_organization_rules]]
