---
name: 第三方库管理规则
description: 第三方库预编译包缓存于third_party/并提交git，CMake直接使用无需vcpkg；开发者通过vcpkg获取库后缓存
metadata: 
  node_type: memory
  type: feedback
  originSessionId: dcb54c27-5e95-4767-8b85-77b263a9317c
---

## 第三方库管理规则

### 核心原则

1. **third_party/ 即完整依赖**：项目选用的所有第三方库预编译产物**必须**存在于 `third_party/<lib>/`，并**已提交 git**
2. **CMake 直接使用缓存**：构建时 CMake 模块直接从 `third_party/` 读取，**不调用 vcpkg**
3. **vcpkg 是开发者工具**：仅在开发者需要新增库或更新版本时，手动通过 vcpkg 下载预编译包，再缓存到 `third_party/`

### 角色分离

| 角色 | 工具 | 说明 |
|------|------|------|
| **CMake 构建时** | 无（直接用 `third_party/`） | 所有开发者 `git clone` 后可直接构建，完全离线 |
| **开发者更新依赖** | vcpkg | 手动下载预编译包 → 按约定重组到 `third_party/` → 提交 git |

### `third_party/<库名>/` 目录结构

```
third_party/<lib>/
├── include/          # 头文件（所有平台共用）
├── lib/<平台>/       # 库文件（导入库/静态库），按平台区分
└── bin/<平台>/       # 运行时文件（DLL），按平台区分（仅 Windows）
```

- 平台标识格式：`<OS>-<Arch>`，如 `Windows-x64`、`macOS-ARM64`、`Linux-x64`
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
- 缓存缺失时 `FATAL_ERROR`（不应该发生，因为已提交 git）

现有模块：
- `cmake/SetupOpenSSL.cmake` → `OpenSSL::SSL`、`OpenSSL::Crypto`
- `cmake/SetupLibJpegTurbo.cmake` → `LibJpegTurbo::turbojpeg`

**Why:** `third_party/` 提交 git 后，所有开发者 clone 即可离线构建，无需安装 vcpkg 或联网下载。vcpkg 回归其本质角色——开发者获取预编译包的工具，而非构建系统的一部分。

**How to apply:**
- CMake 模块只做检测和 IMPORTED target 创建，不调用 vcpkg
- 新增库：开发者手动 vcpkg 下载 → 缓存到 `third_party/` → 提交 git
- 更新版本：开发者删除旧缓存 → vcpkg 下载新版 → 缓存 → 提交 git
