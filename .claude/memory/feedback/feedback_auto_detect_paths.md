---
name: 路径自动检测
description: 项目中所有路径必须使用自动检测，禁止硬编码绝对路径
metadata:
  type: feedback
---

项目中所有路径引用必须使用自动检测机制，禁止硬编码绝对路径（如 `D:\AICode\CrossRemoteDesktop`）。

**Why:** 硬编码路径在跨设备、跨项目复制时立即失效。脚本和配置文件需要在任何项目、任何设备上零修改运行。

**How to apply:**
- **Batch 脚本**：用 `%~dp0` 获取脚本所在目录，推导项目根目录
- **Shell 脚本**：用 `$(cd "$(dirname "$0")/.." && pwd)` 获取项目根目录
- **CMake 配置**：用 `${CMAKE_SOURCE_DIR}` / `${CMAKE_CURRENT_SOURCE_DIR}`
- **AGENTS.md**：用相对于项目根目录的路径，不写绝对路径
- **Memory 文件**：同理使用相对项目根目录的路径
- 需要匹配 Claude Code 项目 slug 时，从当前路径自动推导（`\` → `-`，`:` → `-`）
