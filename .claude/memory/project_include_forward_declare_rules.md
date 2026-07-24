---
name: Include 前向声明规范
description: .h / .cpp 文件中 #include 的组织、路径格式与前向声明判断规则
metadata:
  node_type: memory
  type: project
---

## 适用范围

| 规则 | `.h` | `.cpp` |
|------|------|--------|
| include 路径格式（禁止 `../`，统一 `src/` 相对路径） | ✅ | ✅ |
| include 区块排序（标准库 → Qt → 项目内部 → 前向声明） | ✅ | ✅（前向声明区块省略） |
| Qt / 标准库格式（`<QtModule/Class>`、具体子头） | ✅ | ✅ |
| 前向声明 vs include 决策树 | ✅ | ❌ 不适用 — `.cpp` 直接 include 完整定义 |
| `.cpp` 额外规则 | — | 直接 include 所有实际使用的类型，不做前向声明 |

## 核心原则

`.h` 文件只暴露接口，最小化编译依赖。**优先前向声明，仅必要时 #include。**

`.cpp` 文件是编译终点，**需要什么就 include 什么**——不用考虑传递依赖，不写前向声明。

## 判断规则

在 `.h` 文件中使用类型 T 时，按以下流程判断：

### 必须 #include 的情况

- 作为本类的**基类**
- 作为**值类型成员变量**（含 `std::unique_ptr<T>`、`std::vector<T>`、`std::atomic<T>` 等）
- 作为**容器值参数**（如 `QList<T>`、`std::map<K,V>`）
- 作为**函数返回值或按值传参**
- `.h` 中**内联函数体**访问了 T 的成员或方法
- T 是 **enum class** 且用作值类型（enum class 无法前向声明）
- T 是**模板类**且需要作为值类型使用

### 前向声明 `class T;` 的情况

- 仅以 **T\***（指针）或 **T&**（引用）出现
- 信号参数 `const T&` — **仅限 DirectConnection**（同线程直接连接）。若信号通过 **QueuedConnection** 跨线程传递（如本项目 Worker → ThreadManager 架构），MOC 生成的代码需 `Q_DECLARE_METATYPE` 可见方可复制参数，此时必须 `#include` 完整类型。判定方法：查看该信号的所有 connect() 调用，若有跨线程连接或无法确定 → 保守 `#include`。
- 模板类仅以指针/引用出现：`template<typename T> class Foo;`

### 既不需要 include 也不需要前向声明的情况

- T 仅出现在 **.cpp 定义的方法体**中（`.h` 声明中完全不涉及 T）
  - `.cpp` 自行 `#include "T.h"` 即可

## 分库策略

| 类型来源 | 规则 | 原因 |
|----------|------|------|
| 项目内部 `"common/..."` | 严格按上述判断规则，优先前向声明 | 变化频繁，减少级联重编译 |
| Qt 库 `<QWidget>` 等 | 用到即 include，不做前向声明 | MOC 兼容性，且 Qt 头文件变化少 |
| 标准库 `<memory>` 等 | 用到即 include，不做前向声明 | 标准库变化极少，且标准库类名可能因实现而异 |

## 特殊情况

- **`std::unique_ptr<T>` 成员**：必须在 `.h` 中 `#include "T.h"`。虽然理论上可以在 `.cpp` 析构函数中处理，但规则一致性优先——值类型成员需完整定义。
- **前向声明 struct**：`struct CapturedFrame;` 同样遵循上述规则（struct 与 class 在前向声明中可互换）。
- **前向声明 template class**：`template<typename T> class TripleBuffer;`

## 头文件组织模板

所有 `.h` 文件按以下统一结构组织（空行分隔区块）：

```cpp
#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>
#include <QtNetwork/QSslSocket>

#include "common/error/RdError.h"
#include "common/threading/Worker.h"

class QCloseEvent;
class ServerSession;
class ThreadManager;
struct CapturedFrame;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE
```

### 各区块规则

| 区块 | 排序规则 | 块尾 |
|------|----------|------|
| 标准库 `<...>` | 字母序 | 空一行 |
| Qt `<QtModule/Class>` | 先按模块字母序（Core → Gui → Network → Widgets），模块内类名字母序 | 空一行 |
| 项目内部 `"..."` | 按路径字母序（common/ < client/ < server/ < app/） | 空一行 |
| 前向声明 `class Xxx;` | 字母序 | — |

### 设计决策

- **Qt 内部按模块再分组**：Qt 模块有层次依赖（Core < Gui < Widgets < Network），按序排列便于发现跳级依赖
- **标准库不做前向声明**：不使用 `<iosfwd>` 等标准前向声明头，应用程序代码直接 include 完整头文件更清晰
- **不使用 Qt 聚合头**：始终写具体子头 `<QtCore/QObject>` 而非 `<QtCore>`——编译更快，依赖更明确
- **不使用 `class` 前向声明 Qt 类型**：Qt 类型统一 include（MOC 兼容性），前向声明区仅放 Qt 事件类（`QCloseEvent` 等）
- **项目内部 include 统一使用 `src/` 相对路径**：始终写 `"common/error/RdError.h"` 而非 `"../../common/error/RdError.h"` 或 `"error/RdError.h"`。CMake 已配置 `target_include_directories(... PRIVATE src/)`，任何 `../`、`../../` 等文件相对路径一律禁止。同目录文件使用裸文件名（`"Worker.h"`）。此规则适用于 `.h` 和 `.cpp`。

**Why:** `.h` 是接口文档——读者应一眼看到外部依赖（include）和弱引用（前向声明）的区别。统一的区块顺序消除了"新增 include 放哪儿"的选择成本，团队无歧义。

**How to apply:** 新建或修改 `.h` 文件时，遵循判断流程图和模板；`.cpp` 文件遵循路径格式和区块排序（省略前向声明区块）；对现有文件在编辑到附近时逐步迁移。
