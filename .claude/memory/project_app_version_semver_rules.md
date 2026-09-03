---
name: ""
description: 修改代码时如何根据改动类型自动递增 AppVersion 三段版本号
metadata: 
  node_type: memory
  type: project
  originSessionId: ae67bb09-0bd1-4511-b115-c10f077d6e44
  modified: 2026-08-06T01:34:32.199Z
---

# 应用版本号语义与递增规则

版本号遵循 semver 三段式 `major.minor.patch`（如 `1.0.3`）：

| 分段 | 何时递增 | 本项目判定 |
|------|---------|-----------|
| **major** | 破坏性线上格式/协议变更——旧二进制连不上新二进制 | 帧头尺寸变化、消息结构体字段替换/删除、枚举值重排、握手流程不兼容变更 |
| **minor** | 新增功能，但不破坏既有互通——旧对端能正常对话只是用不到新功能 | 新增消息类型、新增 UI 控件、新增可选的协议扩展 |
| **patch** | 纯 bug 修复，零功能变化、零线上格式变化 | 修竞态、修内存泄漏、修边界条件、日志/注释修正 |

**本项目特殊约束**：握手版本认证采用**完全相等**规则（三段全同才通过）——因此不管升哪段，新旧二进制必断连。规则仅决定"改了什么语义"而非"是否兼容"。

**递增决策**：
- 变更涉及 `ProtocolConstants.h`（除 `AppVersion` 本身）、`Protocol.h`（消息结构体/枚举）、`MessageCodec.cpp`（线上编解码）、`ProtocolImpl.cpp`（帧层）→ **升 major**
- 变更仅新增代码路径、新增文件、新增 UI，不改变既有线上格式 → **升 minor**
- 变更仅修复 bug、调整日志/注释，无线上格式变化 → **升 patch**

**版本号位置**（唯一版本源）：
- `CMakeLists.txt` → `project(CrossRemoteDesktop VERSION x.y.z)` — 需改
- `src/main.cpp` → `const QString APP_VERSION(QString::fromLatin1(APP_VERSION_STR))` — **由 CMake `target_compile_definitions` 自动传入，不需手动改**

**更新版本号后必须同步更新**：
- `README.md` — 版本徽章/版本号引用
- `AGENTS.md` — 构建说明中的版本号引用（如有）

**Why:** CMake `project()` 是版本唯一源，`target_compile_definitions(APP_VERSION_STR)` 将版本传入 C++ 代码。README 和 AGENTS.md 中的版本引用需手动同步以避免文档与二进制不一致。

**How to apply:** 修改 `CMakeLists.txt` 中 `project(VERSION x.y.z)` 后，搜索 `README.md` 和 `AGENTS.md` 中的旧版本号并替换。

**关联**：[[project_overview]]、[[project_constants_organization_rules]]
