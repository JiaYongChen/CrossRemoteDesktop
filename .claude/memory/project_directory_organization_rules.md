---
name: 源码目录组织规范
description: 源码目录组织规范，新增或移动文件时必须遵守
metadata: 
  node_type: memory
  type: project
  originSessionId: 1c9f564d-34b7-4822-a726-bc994eda26c9
---

# 源码目录组织规范

## 顶层结构

```
src/
├── main.cpp              (应用入口)
├── app/                  (应用壳层 — 窗口类、生命周期管理)
├── ui/                   (Qt Designer .ui 文件，仅静态布局)
├── client/               (客户端 — 所有仅客户端使用的代码)
├── server/               (服务端 — 所有仅服务端使用的代码)
└── common/               (共享代码 — 客户端和服务端都依赖的模块)
```

## 各目录职责

### `src/app/` — 应用壳层

组装 client/server 两侧、管理应用生命周期的入口层。放置所有顶层窗口类和全局 UI 组件：

- `MainWindow` — 主窗口，组装客户端/服务端两侧
- `SettingsDialog` — 设置对话框
- `ConnectionDialog`、`ConnectionPanel`、`ConnectionCard` — 连接管理 UI
- `NavPanel` — 侧边导航面板
- `MainWindowLayout` — 主窗口布局构建

**归属判断**：同时依赖 client 和 server 两侧，或作为应用全局入口的代码 → `app/`。

### `src/ui/` — Qt Designer 表单

仅放 `.ui` 文件（静态布局定义）。对应的 C++ 窗口类在 `app/` 中。

### `src/client/` — 客户端

仅客户端使用的代码。当前子目录：

| 子目录 | 职责 |
|--------|------|
| `clipboard/` | 剪贴板同步（ClipboardManager） |
| `decode/` | JPEG 解码（CPU/GPU/NvJpeg） |
| `managers/` | 客户端管理器（DecodeWorker） |
| `network/` | 客户端网络（ConnectionManager, TcpClient） |
| `session/` | 客户端会话（SessionManager, RemoteDesktopSession） |
| `windows/` | 客户端窗口（ClientRemoteWindow, GLTextureViewport 等） |

### `src/server/` — 服务端

仅服务端使用的代码。当前子目录：

| 子目录 | 职责 |
|--------|------|
| `capture/` | 屏幕捕获（ScreenCapture, DxgiCapture） |
| `clienthandler/` | 客户端连接处理（ClientHandlerWorker） |
| `dataflow/` | 数据流管线（FrameBroadcaster, QueueManager） |
| `dataprocessing/` | 数据处理（DataProcessingWorker） |
| `listener/` | TCP 监听（TcpListener） |
| `service/` | 服务管理（ServerService, ServerStarter） |
| `session/` | 服务端会话（ServerSession） |
| `simulator/` | 输入模拟器 |

### `src/common/` — 共享代码

客户端和服务端都依赖的核心模块。**平级组织，不嵌套 `core/` 中间层**：

| 子目录 | 职责 |
|--------|------|
| `config/` | 编译期常量 + 运行时配置（SettingsManager） |
| `crypto/` | 加密（Encryption） |
| `data/` | 共享数据结构（CapturedFrame, PixelFormat） |
| `error/` | 错误类型（RdError, ErrorCode） |
| `logging/` | 日志分类（LoggingCategories） |
| `network/` | 网络协议（Protocol, MessageCodec） |
| `theme/` | 主题管理（ThemeManager） |
| `threading/` | 线程基础设施（Worker, ThreadManager, ThreadSafeQueue） |

## 决策树

新增文件时按以下流程选择归属：

```
该文件 →
├─ 同时依赖 client 和 server 两侧？（如 MainWindow）
│   → src/app/
├─ 仅客户端使用？
│   → src/client/<子目录>/
├─ 仅服务端使用？
│   → src/server/<子目录>/
├─ 客户端和服务端都依赖？（如 Protocol, CapturedFrame, Worker）
│   → src/common/<子目录>/
└─ 是 .ui 静态布局文件？
    → src/ui/
```

## 命名规范

- **目录名**：`snake_case` 或单小写词（如 `clienthandler/`），保持项目现有风格
- **文件名**：`PascalCase`（如 `MainWindow.cpp`、`CapturedFrame.h`）
- **禁止**：目录名使用复数形式不一致（统一用单数或复数，如 `client/window/` 统一为复数）

## Include 路径

头文件引用使用相对于 `src/` 的路径（CMake 已将 `src/` 设为 include 目录）：

```cpp
#include "app/MainWindow.h"
#include "common/config/NetworkConstants.h"
#include "client/session/SessionManager.h"
#include "server/capture/ScreenCapture.h"
```

## 禁止事项

- 禁止在 `common/` 中放置仅单侧使用的代码
- 禁止在 `common/` 下嵌套 `core/` 中间层
- 禁止在 `app/` 中放置 .ui 文件（应放 `ui/`）
- 禁止创建仅含 1 个头文件的碎片子目录（优先合并到相关目录）

**Why:** 当前结构存在 `common/core/` 多余中间层、`common/windows/` 命名混淆（"windows" 被误读为操作系统）、ClipboardManager 仅客户端使用却放在 common、.ui 文件与 C++ 窗口类分离在不同目录等问题。统一后每个目录职责单一，新开发者可以仅凭目录名理解代码归属。

**How to apply:** 新增文件时参照决策树选择目录。移动现有文件时同步更新所有 `#include` 引用。Code review 时检查新增文件是否放对目录。
