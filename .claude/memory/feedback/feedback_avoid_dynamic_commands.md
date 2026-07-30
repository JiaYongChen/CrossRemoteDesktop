---
name: avoid-dynamic-expression-commands
description: 避免以 $变量/$env:/&/$() 开头的动态表达式 PowerShell 命令——Claude Code 对此有不可绕过的弹窗硬闸，频繁打扰用户
metadata:
  node_type: memory
  type: feedback
---

在本项目运行 PowerShell 时，尽量避免以 `$变量`、`$env:X=`、`&`、`$()` 等开头的「动态表达式」命令；优先使用静态命令名前缀（`git`/`cmake`/`Get-ChildItem` 等）。

**Why**：经实证 + 官方文档确认，Claude Code 对无法静态解析命令名的命令采取 fail-closed 强制人工确认（文案 "Command name is a dynamic expression which cannot be statically validated"）。官方明示 "Hook decisions don't bypass permission rules… Hooks can tighten restrictions but not loosen them"——即 PreToolUse hook 返回 `permissionDecision:"allow"` 也**无法**豁免动态表达式弹窗。`permissionDecisionReason` 仅对 `deny` 生效，`ask` 只触发默认弹窗。用户（[[user_profile]]）明确对频繁弹窗表示困扰。

**How to apply**：
- 优先用静态命令名前缀的命令（可被 hook allow 层与 settings.json `PowerShell(前缀 *)` 规则豁免）。
- 确需复杂逻辑时，写入 `.ps1` 文件并以 `pwsh -File xxx.ps1` 调用（配 `PowerShell(pwsh *)` 规则）落入前缀白名单；或建议用户切换 **auto mode**（官方推荐的少弹窗方案，由独立分类器逐动作审查）。
- 不要为了「验证/测试」堆砌 `$env:X=...; foreach(...){...}` 多语句脚本去骚扰用户。
- safety-guard hook（`.claude/scripts/safety-guard.ps1`）的正确定位：**deny 层**（全模式甚至 bypassPermissions 下都生效，不可绕过）+ **静态命令名的 allow 层**；它无法消除动态命令弹窗，那不是 bug 而是 Claude Code 安全模型。
