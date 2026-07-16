# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在本仓库中工作时提供指导。

> 最后更新：2026-07-13

## 首次设置（新设备）

```bash
# 建立 Claude 项目记忆 symlink（使 memory/ 跟随 git 跨设备同步）
# Windows:
scripts\setup-memory-symlink.bat

# macOS / Linux:
chmod +x scripts/setup-memory-symlink.sh && ./scripts/setup-memory-symlink.sh
```

项目记忆（编码规范、反馈、工作流）存放在 `.claude/memory/`（已提交 git）。
上述脚本将 Claude 的本地记忆路径通过 symlink 重定向到此目录。

## 构建命令

```bash
# 配置（在项目根目录执行，创建 build/ 目录）
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 构建主应用程序
cmake --build build --config Debug

# 构建单个测试目标
cmake --build build --config Debug --target test_threadmanager

# 运行所有测试
cd build && ctest -C Debug --output-on-failure

# 按标签运行测试
cd build && ctest -C Debug --output-on-failure -L unit
cd build && ctest -C Debug --output-on-failure -L integration

# 按名称运行单个测试
cd build && ctest -C Debug -R ThreadManagerTest

# 自定义聚合测试目标
cmake --build build --target run_all_tests
cmake --build build --target run_quick_tests
cmake --build build --target run_core_tests
cmake --build build --target run_threading_tests
cmake --build build --target run_performance_tests

# 同步 TS 文件（修改源码 tr() 调用或 .ui 文件后执行，仅扫描不编译）
cmake --build build --target update_translations

# 编译 QM 文件（TS 翻译完成后执行，生成二进制翻译文件）
cmake --build build --target release_translations

# 一键完成：lupdate + lrelease（源码扫描 → TS → QM）
cmake --build build --target sync_translations
```

**注意**：Windows/MSVC 多配置生成器下 `ctest` 必须加 `-C Debug`。

输出目录：项目根目录下的 `Debug/` 和 `Release/`（不在 `build/` 内）。
测试自动使用 `QT_QPA_PLATFORM=offscreen`（在 CMake 中设置），适用于无头/CI 环境。

**翻译文件维护**：修改源码中的 `tr()` 调用或 `.ui` 文件后，运行 `cmake --build build --target sync_translations` 一键完成扫描+编译（`lupdate` → `lrelease`）。完整流程和常见问题见 [[翻译系统维护规范]]。

## 依赖

- **Qt 6.9+**: Core, Widgets, Network, Gui, OpenGL, OpenGLWidgets, Concurrent, Svg（Test 仅测试目录使用）
- **OpenSSL**: 缓存于 `third_party/openssl/`，开发者通过 vcpkg 获取。
- **libjpeg-turbo**: 同上，缓存于 `third_party/libjpeg-turbo/`。
- **nvJPEG**（可选）: CUDA 12.x SDK 缓存于 `third_party/nvjpeg/`。缺失时自动跳过，运行时降级为 CPU 解码。开发者通过 CUDA Toolkit 获取。
- **C++20** 必需，CMake 3.16+

### 第三方库管理

> 详细规则（目录结构、CMake 模块模式、开发者流程）见 [[第三方库管理规则]]。

项目使用 [vcpkg](https://github.com/microsoft/vcpkg) 作为开发者获取预编译包的工具。**CMake 构建时不调用 vcpkg**——所有产物缓存于 `third_party/` 并提交 git，构建时直接使用，支持 `git clone` 后离线构建。

**开发者流程**：vcpkg 下载 → 按约定重组到 `third_party/<lib>/` → 提交 git。

### CMake 模块结构

根 CMakeLists.txt 通过 `include()` 引入 7 个模块化 .cmake 文件：

```
cmake/
├── SetupOpenSSL.cmake        # OpenSSL — 直接使用 third_party/ 缓存
├── SetupLibJpegTurbo.cmake   # libjpeg-turbo — 直接使用 third_party/ 缓存
├── SetupNvJpeg.cmake         # nvJPEG（可选）— third_party/ 缓存，缺失时自动跳过
├── PlatformDetect.cmake      # OS/架构自动识别
├── SetupQt6.cmake            # Qt6 路径搜索 + 架构验证
├── FilterAppleOpenGL.cmake   # macOS AGL/OpenGL 过滤
├── DeployWindowsDlls.cmake   # Windows 运行时 DLL 部署
└── TestHelpers.cmake         # add_rd_test() 测试辅助函数
```

测试定义使用 `add_rd_test()` 函数（封装 `qt_add_executable → target_link_libraries → add_test → set_tests_properties` 全流程），单个测试定义由 ~45 行缩减至 ~8 行。

## 架构

本项目是基于 Qt 的远程桌面应用，客户端和服务端集成在单个可执行文件中。

### 依赖注入链（DI）

所有核心组件通过构造函数注入，**没有全局单例**。注入起点为 `MainWindow` 构造函数：

```
MainWindow → new ThreadManager(this)
          → new QueueManager(this)
          → new ServerService(m_threadManager, m_queueManager)（服务端门面）
                → [start()] new TcpListener（监听端口，接受连接）
                → [start()] new CapturePipeline（屏幕捕获 + 帧广播）
                      → new ScreenCapture(m_threadManager, m_queueManager)
                      → new FrameBroadcaster(m_queueManager)
                → [onNewConnection] new ServerSession（每客户端独立会话）
                      → SessionQueuePair（每会话私有队列对）
                      → DataProcessingWorker（JPEG 编码）
                      → ClientHandlerWorker（发送到客户端）
```

**说明**：`ServerService`（`src/server/service/`）是服务端编排门面，管理 `TcpListener` + `CapturePipeline` + 会话生命周期，从 MainWindow 分离以提升可测试性。`ServerSession` 在 `TcpListener::newConnection` 信号触发时按需创建，每个客户端连接对应一个独立会话。

### 服务端会话架构

```
TcpListener（接受连接）
  └─ newConnection(qintptr) → ServerService 创建 ServerSession
                                  ├── SessionQueuePair（私有队列对）
                                  │     ├── captureQueue（FrameBroadcaster → 编码）
                                  │     └── processedQueue（编码 → 网络发送）
                                  ├── DataProcessingWorker（JPEG 编码线程）
                                  └── ClientHandlerWorker（网络发送线程）

CapturePipeline（捕获线程）
  ├── ScreenCapture（屏幕捕获 + ScreenCaptureWorker 子线程）
  └── FrameBroadcaster（帧广播器）
        └── subscribe/unsubscribe → 广播到各 ServerSession::enqueueFrame()
```

### 服务端数据管线（生产者-消费者模式）

```
ScreenCapture → QueueManager(captureQueue)
  → FrameBroadcaster（拉帧，广播到所有订阅 session）
    → SessionQueuePair.captureQueue（每 session 私有）
      → DataProcessingWorker（JPEG 编码）
        → SessionQueuePair.processedQueue
          → ClientHandlerWorker（TCP 发送）
```

- **QueueManager** 拥有共享的 `ThreadSafeQueue<CapturedFrame>`（捕获队列，Drain-to-Latest 语义）
- **SessionQueuePair** 是每 session 私有的队列对（纯数据结构，非 QObject），包含 captureQueue 和 processedQueue
- **FrameBroadcaster** 从共享队列拉帧，通过 `QMetaObject::invokeMethod` 跨线程投递到各 ServerSession
- 通过 `QueueManager::initialize(captureQueueSize)` 初始化（默认 `MAX_QUEUE_SIZE`）
- Worker 直接从队列拉取数据（非信号槽），以获得更高性能

### 客户端架构

```
ConnectionManager (TCP) → SessionManager (状态管理) → ClientRemoteWindow (QWidget)
                                                       ├── GLTextureViewport（OpenGL渲染）
                                                       ├── InputForwarder（键鼠事件转发）
                                                       ├── ConnectionLifecycle（状态+断连管理）
                                                       ├── RenderManager（坐标映射）
                                                       ├── CursorManager（光标显示）
                                                       └── ClipboardManager（剪贴板同步）
```

**ClientRemoteWindow（375 行）** 通过提取两个协调类聚焦于 QWidget + GL 视口管理：
- `InputForwarder`：事件过滤器，处理全部鼠标/键盘/滚轮事件转发
- `ConnectionLifecycle`：连接状态机 + 窗口标题同步 + 断连对话框

### 线程模型

所有 Worker 继承 `Worker` 基类（`src/common/threading/Worker.h`），提供：
- 状态机：Stopped → Starting → Running ⇄ Paused → Stopping → Stopped
- 信号：started, stopped, paused, resumed, errorOccurred
- 重写 `processTask()` 实现工作循环，`initialize()`/`cleanup()` 管理生命周期
- 性能追踪：在 processTask 循环中使用 `startPerformanceTiming()` / `endPerformanceTiming()`

**ThreadManager** 管理所有 Worker 线程（通过依赖注入传递，非单例）：
- `createThread(name, unique_ptr<Worker>, autoStart, autoRestart, maxRestarts)` 注册 Worker
- `startThread/stopThread/pauseThread/resumeThread/destroyThread` 单独控制
- `startAllThreads/stopAllThreads/destroyAllThreads` 批量控制
- 所有返回 bool 的方法已添加 `[[nodiscard]]`

### 错误处理系统

**统一错误类型**（`src/common/error/`）：
- `RdError`：轻量值类型结构体，含 `code`（ErrorCode 枚举）、`message`、`source`、`timestampMs` 四字段
- `ErrorCode`：26 个枚举值，按模块分类（网络/认证/会话/捕获/数据处理/队列/线程/服务端/配置）
- 项目中 13 个错误信号全部迁移到 `const RdError&` 参数
- 可为跨线程传递（`Q_DECLARE_METATYPE`）

### 网络协议（RDCP）

定义在 `src/common/network/Protocol.h`：
- 魔数：`0x52444350`，28 字节头部（魔数 + 版本 + 类型 + 长度 + 校验和 + 时间戳）
- 关键消息类型：HANDSHAKE, AUTHENTICATION, HEARTBEAT, SCREEN_DATA, MOUSE_EVENT, KEYBOARD_EVENT, CLIPBOARD_DATA
- 默认端口：5921（见 `src/common/config/NetworkConstants.h`）
- 校验和：CRC-32（非安全哈希，仅完整性校验）
- **已移除协议**：AUDIO_DATA/...（音频未实现）、FILE_TRANSFER_*/...（文件传输未实现）

序列化实现物理分离：
- `ProtocolImpl.cpp`（120 行）— Protocol 静态函数 + CRC32 校验和
- `MessageCodec.cpp`（~530 行）— 14 个消息结构体的 encode/decode 方法

### 配置

运行时配置通过 `SettingsManager` 类管理。编译期常量分布在 6 个域名空间文件中（`src/common/config/`）：

| 文件 | 命名空间 | 领域 |
|------|----------|------|
| `ProtocolConstants.h` | `ProtocolConstants` | 协议标识、消息字段长度、帧尺寸、应用版本 |
| `NetworkConstants.h` | `NetworkConstants` | 连接超时、心跳、缓冲区、默认端口 |
| `CaptureConstants.h` | `CaptureConstants` | 捕获帧率、JPEG质量、缩放因子、输入参数 |
| `ProcessingConstants.h` | `ProcessingConstants` | 线程池、队列、缓冲区、性能阈值 |
| `SecurityConstants.h` | `SecurityConstants` | 加密参数、认证限制、会话超时 |
| `GuiConstants.h` | `GuiConstants` | 窗口尺寸、OpenGL渲染、帧丢弃策略 |

**常量规范**：统一 `namespace` + `inline constexpr`，`PascalCase` 命名。完整决策树、类型规范、禁止事项见 [[常量组织规范]]。2 个及以上文件引用的常量必须放入公共文件。

## Git 提交规范

- **禁止在 commit message 末尾追加 `Co-Authored-By` 署名**，所有提交信息仅包含用户指定的内容。

## 日志规范

> 详细规则见 [[日志规范]]，本节为日志分类树和级别语义概要参考。

**所有日志必须使用分类流式宏：**
```cpp
qCInfo(lcServer) << "message" << variable;    // 正确
qCWarning(lcXxx, "fmt %s", arg);              // 错误 - 禁止 printf 风格
qWarning() << "message";                      // 错误 - 禁止无分类日志
```

### 日志分类树（六棵一级树）

| 树根 | 分类变量 | 覆盖范围 |
|------|---------|----------|
| `app` | `lcApp` | 应用入口、生命周期、Translation |
| `core.*` | `lcCoreProtocol`, `lcCoreThreading`, `lcCoreConfig` | 协议、线程、配置 |
| `server.*` | `lcServer`, `lcServerNetwork`, `lcServerCapture`, `lcServerCaptureDxgi`, `lcServerEncode`, `lcServerQueue`, `lcServerClientHandler`, `lcServerInput` | 服务端全链路 |
| `client.*` | `lcClient`, `lcClientSession`, `lcClientSessionDecode`, `lcClientSessionProtocol`, `lcClientGL`, `lcClientRemoteWindow` | 客户端全链路 |
| `ui.*` | `lcUI`, `lcUIMainWindow`, `lcUISettingsDialog` | 用户界面 |
| `test.*` | `lcTest`, `lcTestScreenCapture`, `lcTestScreenCaptureIntegration`, `lcTestClientHandler`, `lcTestProducerConsumer` | 测试 |

**类别字符串命名**：统一 `domain.component[.subcomponent]` 点分格式，默认两级，三级仅用于独立调试子组件。

**默认级别**：所有分类 `QtDebugMsg`，运行时通过 `QLoggingCategory::setFilterRules()` 按需控制。

错误日志推荐使用 `RdError::logLabel()` 而非裸字符串：
```cpp
qCWarning(lcServer) << error.logLabel();      // 推荐
```

所有日志分类在 `src/common/logging/LoggingCategories.h` 中声明，在对应 `.cpp` 中定义。禁止在其他文件中定义 `Q_LOGGING_CATEGORY` 或 `Q_DECLARE_LOGGING_CATEGORY`。新增分类时必须添加到 `LoggingCategories.h/.cpp`。

### 日志级别语义规范

| 级别 | 受众 | 语义 | 示例 |
|------|------|------|------|
| `qCDebug` | 开发者 | 开发期追踪，正常运行时无意义 | 构造/析构、状态跃迁、算法中间值 |
| `qCInfo` | 运维/用户 | 可观测的运行时事件 | 服务启动/停止、客户端连接/断开、初始化成功 |
| `qCWarning` | 运维/开发者 | 可恢复异常，系统已自行处理 | 重试成功、降级方案、资源临时不足 |
| `qCCritical` | 所有人 | 不可恢复错误，功能受损 | 异常崩溃、关键线程失败、数据损坏 |

**判定规则：**
1. "如果这条日志没开，你会后悔吗？" — 后悔 → qCInfo；不 → qCDebug
2. "这条日志出现在正常生产环境合理吗？" — 合理 → qCInfo；不合理 → qCDebug
3. "系统自己修好了吗？" — 修好了 → qCWarning；没修好 → qCCritical

**默认过滤建议：** 运行时设置 `*.debug=false` 即可获得有意义的运维视图。

## 编译器设置

- **警告视为错误**，全局启用：`-Werror`（GCC/Clang）、`/WX`（MSVC）
- MSVC 额外使用：`/utf-8`、`/Zc:__cplusplus`、`NOMINMAX`、`WIN32_LEAN_AND_MEAN`
- 测试目标使用更严格的 `-Wall -Wextra -Wpedantic -Werror`
- **代码风格**：全部 encode/decode 方法、生命周期方法（initialize/startThread/...）已添加 `[[nodiscard]]`

## 平台特定代码

输入模拟器有平台变体，位于 `src/server/simulator/` 下按平台分子目录：`windows/`、`macos/`、`linux/`。CMake 通过 GLOB_RECURSE 自动收集全部源文件，平台选择通过文件内 `#ifdef Q_OS_*` 编译期守卫实现。

## 测试

26 个测试目标（含 32 个 SessionManager 专用用例），测试目录镜像 `src/` 结构按模块组织：
- **`test/app/`** — 应用壳层测试
- **`test/client/`** — 客户端测试（`decode/`、`session/`、`windows/`、`network/`）
- **`test/server/`** — 服务端测试（`capture/`、`clienthandler/`、`dataprocessing/`、`dataflow/`）
- **`test/common/`** — 共享代码测试（`threading/`）
- **`test/integration/`** — 跨模块集成测试
- **MockConnectionManager**：重写 `isConnected()`/`isAuthenticated()` 等 virtual 方法，用于 SessionManager 测试（`test/client/session/test_sessionmanager_common.h`）
- **SessionManager DI**：受保护构造函数 + friend 声明，测试可直接注入 MockConnectionManager
- **`add_rd_test()`**：测试目标创建辅助函数（见 `cmake/TestHelpers.cmake`），支持 NO_OPENGL 标志、EXTRA_SOURCES、EXTRA_ENV 等可选参数

## 已移除的功能

- **音频协议**：AUDIO_DATA/AUDIO_FORMAT 枚举 + AudioData 结构体 + AudioSettings 配置（未实现）
- **文件传输协议**：FILE_TRANSFER_* 枚举 + FileTransferStatus + FileTransferRequest/Response/FileData（未实现）
- **FileTransferManager**：整类删除（仅处理客户端拖放 UI，无服务端处理）
- **性能叠加层**：ClientRemoteWindow::drawPerformanceInfo()（m_showPerformanceInfo 恒为 false）
- **ServerManager / ServerWorker**：整类删除，服务端编排逻辑由 `ServerService`（`src/server/service/`）接管，`TcpListener` + `CapturePipeline` + 会话管理统一封装
- **ConfigBinding<T> 模板类** + 7 个 CONFIG_* 便利宏（零引用死代码）

## 代码风格

### Include / 前向声明

`.h` 文件中的 `#include` 和前向声明遵循统一规范（详见 [[Include 前向声明规范]]）：

- **项目内部类型**：优先前向声明，仅值类型 / 基类 / 内联访问成员时 `#include`
- **Qt 类型**：用到即 `#include`，不做前向声明（MOC 兼容）
- **标准库**：用到即 `#include`，不做前向声明
- **模板**：头文件按 标准库 → Qt → 项目内部 → 前向声明 四区块组织，每区块字母序
- **仅 .cpp 用到的类型**：`.h` 中不写任何东西，`.cpp` 自行 include
