---
name: 第三方库管理规则
description: 支持平台的第三方预编译包缓存于third_party/并提交git，CMake直接使用无需vcpkg；开发者通过vcpkg获取库后缓存
metadata: 
  node_type: memory
  type: feedback
  originSessionId: dcb54c27-5e95-4767-8b85-77b263a9317c
---

## 第三方库管理规则

### 核心原则

1. **完整缓存是平台支持条件**：被声明为“缓存完整”的目标平台，其必需第三方库预编译产物**必须**存在于 `third_party/<lib>/`，并**已提交 git**
2. **CMake 直接使用缓存**：构建时 CMake 模块直接从 `third_party/` 读取，**不调用 vcpkg**
3. **vcpkg 是开发者工具**：仅在开发者需要新增库或更新版本时，手动通过 vcpkg 下载预编译包，再缓存到 `third_party/`
4. **区分当前状态与目标状态**：Windows x64、Linux x64 为基础缓存完整平台；Linux 新增文件必须与配置修改原子提交。macOS CMake 接入已完成，但缓存当前不完整，不能宣称克隆后离线构建或目标平台已验证

Qt SDK、Linux 系统开发包，以及当前显式禁用的 PipeWire/VA-API 不属于 `third_party/` 基础缓存承诺；它们的要求见 `docs/BUILD.md`。

### 角色分离

| 角色 | 工具 | 说明 |
|------|------|------|
| **CMake 构建时** | 无（直接用 `third_party/`） | 缓存完整的平台可直接读取预编译库；Qt/系统依赖仍按构建指南准备 |
| **开发者更新依赖** | vcpkg | 手动下载预编译包 → 按约定重组到 `third_party/` → 提交 git |

### `third_party/<库名>/` 目录结构

```
third_party/<lib>/
├── include/          # 公共头文件
├── include/<平台>/   # 平台专用头（仅依赖需要时）
├── lib/<平台>/       # 库文件（导入库/静态库），按平台区分
└── bin/<平台>/       # 运行时文件（DLL），按平台区分（仅 Windows）
```

- 平台标识格式：`<OS>-<Arch>`，如 `Windows-x64`、`macOS-ARM64`、`Linux-x64`
- OpenSSL 的公共头位于 `include/openssl/`，平台生成的 `configuration.h` 位于 `include/<平台>/openssl/`；CMake 必须将平台 include 路径放在公共路径之前
- OpenSSL 消费方优先链接 `OpenSSL::SSL` / `OpenSSL::Crypto`；确需路径变量时使用完整的 `OPENSSL_INCLUDE_DIRS`。项目不导出容易遗漏平台配置目录的单数 `OPENSSL_INCLUDE_DIR`
- Debug DLL 使用大写 `D` 后缀区分：`turbojpegD.dll`（Debug）vs `turbojpeg.dll`（Release）

### 开发者新增/更新库流程

**前置条件**：需设置 `VCPKG_ROOT` 环境变量（指向 vcpkg 安装目录）。

1. 通过 vcpkg 下载预编译包
2. 将产物按上述目录结构重组到 `third_party/<lib>/`
3. 提交 git（所有 `third_party/` 变更必须提交）

### CMake 模块模式

每个第三方库对应一个 `cmake/Setup<LibName>.cmake`：
- 直接从 `third_party/<lib>/` 读取头文件和库文件
- 创建对应的 IMPORTED target（如 `OpenSSL::SSL`、`LibJpegTurbo::turbojpeg`）
- 缓存缺失时 `FATAL_ERROR`；这既能阻止误用不完整缓存，也用于明确标识尚未支持的平台

现有预编译缓存模块：
- `cmake/SetupOpenSSL.cmake` → `OpenSSL::SSL`、`OpenSSL::Crypto`
- `cmake/SetupLibJpegTurbo.cmake` → `LibJpegTurbo::turbojpeg`
- `cmake/SetupNvJpeg.cmake` → Windows 的 `Cuda::cudart`、`Cuda::nvjpeg`

**Why:** 目标平台的 `third_party/` 内容完整提交后，开发者无需在 CMake 配置阶段调用 vcpkg 或联网下载这些库。vcpkg 回归其本质角色——开发者获取预编译包的工具，而非构建系统的一部分。

**How to apply:**
- CMake 模块只做检测和 IMPORTED target 创建，不调用 vcpkg
- 新增库：开发者手动 vcpkg 下载 → 缓存到 `third_party/` → 提交 git
- 更新版本：开发者删除旧缓存 → vcpkg 下载新版 → 缓存 → 提交 git
- 移动平台专用头时，公共头删除、平台头新增、库缓存与 CMake 路径修改必须在同一提交中完成
