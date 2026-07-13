# UltraDesktop

一个基于 Qt 6.9+ 的高性能跨平台远程桌面应用程序，支持 Windows、macOS 与 Linux 之间的远程连接与控制。

## 项目概述

本项目采用依赖注入（DI）架构和多线程 Worker 模式，基于生产者-消费者管线实现安全、高效、低延迟的远程桌面访问。已实现屏幕共享、远程输入控制、GPU 加速 JPEG 解码、OpenGL 纹理渲染等核心功能。

## 功能特性

### 已实现 ✅

- **核心功能**
    - 实时屏幕共享（DXGI / Qt 跨平台捕获 → JPEG 编码 → TCP 传输）
    - 远程控制（鼠标 + 键盘：点击、滚轮、组合键）
    - 光标管理：本地光标自动隐藏与远程光标渲染
    - GPU 加速 JPEG 解码（nvJPEG，可选）+ CPU 回退（libjpeg-turbo）
    - OpenGL 纹理视口渲染（`GLTextureViewport`）
    - TCP 连接、心跳与智能重连逻辑

- **架构特性**
    - 依赖注入（DI）：所有核心组件通过构造函数注入，无全局单例
    - 会话化管线：`ScreenCapture → QueueManager → FrameBroadcaster → ServerSession（私有队列对） → DataProcessingWorker → ClientHandlerWorker`
    - 线程安全队列（`ThreadSafeQueue`）：纯队列通信，避免信号槽开销
    - 客户端三缓冲帧管理（`TripleBuffer`）：零拷贝帧共享
    - Worker 基类状态机：Stopped → Starting → Running ⇄ Paused → Stopping → Stopped
    - 统一错误处理（`RdError` + `ErrorCode` 26 个枚举值）
    - 网络协议 RDCP：28 字节头部，CRC-32 校验和，14 种消息类型

- **系统功能**
    - 剪贴板同步（`ClipboardManager`，`src/client/clipboard/`）
    - 明暗双主题 QSS 样式（`light.qss` / `dark.qss`）
    - 多语言支持（zh_CN / en_US），含翻译同步工具链
    - 分类流式日志系统（6 棵一级分类树，按大小滚动）
    - 灵活的配置系统（QSettings，支持运行时更新）
    - 密码认证（`PasswordCrypto`，SHA-256 哈希）
    - 23 个测试目标，覆盖单元 / 集成 / 性能测试（镜像 src/ 结构组织）
    - 跨平台输入模拟（Windows / macOS / Linux 键盘鼠标注入）

### 规划中 ⏳

- 文件传输（双向、断点续传）
- 音频传输（捕获 / 播放，编解码）
- H.264 / H.265 视频编码
- UDP 传输渠道
- 多显示器支持
- TLS/SSL 加密传输

## 系统要求

- **操作系统**：Windows 10+ / macOS 11+ / Linux（实验性支持）
- **内存**：4GB 及以上（推荐 8GB）
- **存储**：100MB 可用空间
- **网络**：TCP/IP 网络环境
- **GPU（可选）**：NVIDIA GPU + CUDA 12.x 用于 nvJPEG 硬件解码加速

## 技术栈

| 类别 | 技术 |
|------|------|
| **框架** | Qt 6.9+（Core / Widgets / Network / Gui / OpenGL / OpenGLWidgets / Concurrent / Svg） |
| **语言** | C++20 |
| **构建** | CMake 3.16+（8 个模块化 .cmake 脚本，跨平台自动检测） |
| **图像编码** | libjpeg-turbo（CPU）+ nvJPEG（GPU 可选，CUDA 12.x） |
| **加密** | OpenSSL（SHA-256 密码哈希 + 网络加密基础） |
| **图形** | OpenGL 3.3+（纹理渲染）、DXGI（Windows 屏幕捕获）、D3D11 |
| **测试** | Qt Test（27 个测试目标，含标签分类：unit / integration / performance） |

## 代码结构

```
UltraDesktop/
├── CMakeLists.txt                    # 主构建配置
├── cmake/                            # CMake 模块（8 个）
│   ├── PlatformDetect.cmake          #  OS / 架构自动识别
│   ├── SetupQt6.cmake                #  Qt6 路径搜索 + 架构验证
│   ├── SetupOpenSSL.cmake            #  OpenSSL（third_party/ 缓存）
│   ├── SetupLibJpegTurbo.cmake       #  libjpeg-turbo（third_party/ 缓存）
│   ├── SetupNvJpeg.cmake             #  nvJPEG 可选（third_party/ 缓存）
│   ├── FilterAppleOpenGL.cmake       #  macOS AGL / OpenGL 过滤
│   ├── DeployWindowsDlls.cmake       #  Windows 运行时 DLL 部署
│   └── TestHelpers.cmake             #  add_rd_test() 测试辅助函数
├── resources/                        # 资源文件
│   ├── icons/                        # SVG 应用图标（35+）
│   ├── styles/                       # QSS 样式（light.qss / dark.qss）
│   ├── translations/                 # 多语言翻译（zh_CN / en_US .ts + .qm）
│   └── resources.qrc                 # Qt 资源集合
├── src/                              # 源代码
│   ├── main.cpp                      # 程序入口
│   ├── app/                          # 应用壳层（窗口类 + 生命周期管理）
│   │   ├── MainWindow.*              # 主窗口（DI 入口，组装客户端/服务端）
│   │   ├── MainWindowLayout.*        # 主窗口布局管理
│   │   ├── ConnectionDialog.*        # 连接对话框
│   │   ├── ConnectionPanel.*         # 连接面板
│   │   ├── ConnectionCard.*          # 连接卡片控件
│   │   ├── ConnectionHistory.*       # 连接历史管理
│   │   ├── NavPanel.*                # 导航面板
│   │   └── SettingsDialog.*          # 设置对话框
│   ├── ui/                           # Qt Designer .ui 文件（6 个，仅静态布局）
│   │   ├── MainWindow.ui
│   │   ├── ConnectionDialog.ui
│   │   ├── ConnectionPanel.ui
│   │   ├── ConnectionCard.ui
│   │   ├── NavPanel.ui
│   │   └── SettingsDialog.ui
│   ├── client/                       # 客户端模块
│   │   ├── clipboard/                # 剪贴板同步
│   │   │   └── ClipboardManager.*
│   │   ├── core/
│   │   │   ├── TripleBuffer.h        # 三缓冲帧管理（零拷贝）
│   │   │   └── FrameSlot.h           # 帧槽位定义
│   │   ├── decode/
│   │   │   ├── IDecoder.h            # 解码器接口
│   │   │   ├── IDecodeTarget.h       # 解码目标接口
│   │   │   ├── TurboJpegDecoder.*    # libjpeg-turbo CPU 解码
│   │   │   ├── NvJpegDecoder.*       # nvJPEG GPU 解码
│   │   │   └── GpuDecodeTarget.*     # GPU 解码目标（CUDA 纹理）
│   │   ├── managers/
│   │   │   └── DecodeWorker.*        # 解码工作线程
│   │   ├── network/
│   │   │   ├── ConnectionManager.*   # TCP 连接管理
│   │   │   └── TcpClient.*           # TCP 客户端实现
│   │   ├── session/
│   │   │   ├── RemoteDesktopSession.* # 会话总控（状态机）
│   │   │   ├── ProtocolSession.*     # RDCP 协议会话
│   │   │   └── DecodePipeline.*      # 解码管线
│   │   └── windows/                  # 客户端窗口
│   │       ├── ClientRemoteWindow.*  # 远程桌面窗口（QWidget + GL）
│   │       ├── GLTextureViewport.*   # OpenGL 纹理视口
│   │       ├── InputForwarder.*      # 键鼠事件转发
│   │       ├── ConnectionLifecycle.* # 连接状态机 + 断连管理
│   │       └── CursorManager.*       # 光标显示管理
│   ├── server/                       # 服务器模块
│   │   ├── listener/
│   │   │   └── TcpListener.*         # TCP 监听器 Worker
│   │   ├── capture/
│   │   │   ├── CapturePipeline.*     # 捕获管线（ScreenCapture + FrameBroadcaster）
│   │   │   ├── ScreenCapture.*       # 屏幕捕获管理
│   │   │   ├── ScreenCaptureWorker.* # 捕获工作线程
│   │   │   ├── FrameBroadcaster.*    # 帧广播器（订阅/广播模型）
│   │   │   ├── DxgiCapture.*         # DXGI 桌面复制（Windows）
│   │   │   └── CaptureConfig.h       # 捕获配置
│   │   ├── session/
│   │   │   ├── ServerSession.*       # 每客户端独立会话 Worker
│   │   │   └── SessionQueuePair.h    # 每会话私有队列对
│   │   ├── clienthandler/
│   │   │   ├── ClientHandlerWorker.* # 客户端处理 Worker
│   │   │   └── AuthHandler.*         # 密码认证处理
│   │   ├── dataflow/
│   │   │   ├── QueueManager.*        # 共享队列管理器（DI 注入）
│   │   │   └── DataFlowStructures.*  # 数据结构定义
│   │   ├── dataprocessing/
│   │   │   ├── DataProcessing.*      # JPEG 编码逻辑
│   │   │   ├── DataProcessingWorker.* # 编码工作线程
│   │   │   └── DataProcessingConfig.* # 编码配置
│   │   ├── service/
│   │   │   ├── TcpServer.*           # QTcpServer 封装（TLS + 监听）
│   │   │   └── ServerService.*       # 服务端编排门面（TcpListener + CapturePipeline + 会话管理）
│   │   └── simulator/                # 输入模拟（按平台分子目录）
│   │       ├── InputSimulator.*      # 输入模拟基类
│   │       ├── KeyboardSimulator.*   # 键盘模拟基类
│   │       ├── MouseSimulator.*      # 鼠标模拟基类
│   │       ├── windows/              # Windows 平台实现
│   │       ├── macos/                # macOS 平台实现
│   │       └── linux/                # Linux 平台实现
│   └── common/                       # 共享代码（8 个平级子目录，无 core/ 中间层）
│       ├── threading/                # 线程管理
│       │   ├── Worker.*              # Worker 基类
│       │   ├── ThreadManager.*       # 线程管理器
│       │   └── ThreadSafeQueue.h     # 线程安全队列
│       ├── logging/
│       │   └── LoggingCategories.*   # 日志分类（6 棵一级树）
│       ├── config/                   # 编译期常量 + 运行时配置
│       │   ├── SettingsManager.*     # 配置管理器（基于 QSettings，含持久化）
│       │   ├── TranslationUtils.*    # 翻译工具函数
│       │   ├── NetworkConstants.h    # 网络常量
│       │   ├── ProtocolConstants.h   # 协议标识、消息字段、帧尺寸
│       │   ├── CaptureConstants.h    # 捕获帧率、JPEG质量、输入参数
│       │   ├── ProcessingConstants.h # 线程池、队列、性能阈值
│       │   ├── SecurityConstants.h   # 加密参数、认证限制
│       │   └── GuiConstants.h        # 窗口尺寸、OpenGL渲染
│       ├── network/
│       │   ├── Protocol.h            # RDCP 协议定义
│       │   ├── ProtocolImpl.cpp      # 协议静态函数 + CRC32
│       │   └── MessageCodec.cpp      # 14 种消息编解码
│       ├── error/
│       │   ├── RdError.h             # 统一错误类型
│       │   └── ErrorCode.h           # 26 个错误码枚举
│       ├── crypto/
│       │   └── PasswordCrypto.*      # 密码哈希与验证
│       ├── theme/
│       │   ├── TitleBarTheme.*       # 标题栏主题适配
│       │   └── IconThemeProvider.*   # SVG 图标主题
│       └── data/
│           ├── ConnectionParams.h    # 连接参数定义
│           └── DataRecord.h          # 数据记录
├── test/                             # 测试套件（23 个测试目标，镜像 src/ 结构）
│   ├── app/                          # 应用壳层测试
│   ├── client/                       # 客户端测试（decode / session / windows / network）
│   ├── server/                       # 服务端测试（capture / clienthandler / dataprocessing / dataflow）
│   ├── common/                       # 共享代码测试（threading）
│   └── integration/                  # 跨模块集成测试
├── third_party/                      # 预编译第三方库缓存（提交 git）
│   ├── openssl/                      # OpenSSL（SSL + Crypto）
│   ├── libjpeg-turbo/                # libjpeg-turbo（JPEG 编解码）
│   └── nvjpeg/                       # nvJPEG + CUDA runtime（可选）
└── docs/                             # 技术文档
    └── project_analysis.md           # 项目分析报告
```

### 核心架构说明

**依赖注入链**（`MainWindow` 构造函数为入口，`ServerService` 管理服务端生命周期）：

```
MainWindow → new ThreadManager(this)
          → new QueueManager(this)
          → new ServerService(m_threadManager, m_queueManager)（服务端编排门面）
                → [start()] new TcpListener（监听端口）
                → [start()] new CapturePipeline
                      → new ScreenCapture（捕获管理）
                      → new FrameBroadcaster（帧广播）
                → [onNewConnection] new ServerSession（每客户端独立会话）
                      → SessionQueuePair（私有队列对）
                      → DataProcessingWorker（JPEG 编码）
                      → ClientHandlerWorker（TCP 发送）
```

**服务端数据管线**（会话化广播模型）：

```
ScreenCapture → QueueManager（共享捕获队列，Drain-to-Latest）
  → FrameBroadcaster（拉帧 → 广播到所有订阅 ServerSession）
    → SessionQueuePair.captureQueue（每 session 私有）
      → DataProcessingWorker（JPEG 编码）
        → SessionQueuePair.processedQueue
          → ClientHandlerWorker（TCP 发送）
```

**客户端解码管线**：

```
ConnectionManager (TCP) → ProtocolSession (RDCP) → DecodePipeline → GLTextureViewport (OpenGL)
                              ↓
                         TripleBuffer ← DecodeWorker (nvJPEG / TurboJpeg)
```

**关键特性**：
- **依赖注入**：所有核心组件通过构造函数注入，无全局单例
- **会话化架构**：每客户端独立 `ServerSession`，`FrameBroadcaster` 一次捕获多方广播，避免重复编码
- **队列驱动**：所有数据传输通过 `ThreadSafeQueue`，避免信号槽开销
- **三缓冲**：客户端使用 `TripleBuffer` 实现解码线程与渲染线程间的零拷贝帧共享
- **GPU 加速**：nvJPEG 硬件解码 + CUDA 纹理（可选），运行时缺失自动降级为 CPU 解码
- **异步处理**：所有 I/O 操作使用 `QMetaObject::invokeMethod` 非阻塞执行

## 编译与构建

```bash
# 配置（在项目根目录执行，创建 build/ 目录）
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 构建主应用程序
cmake --build build --config Debug

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

# 同步翻译文件（修改源码 tr() 调用或 .ui 文件后执行）
cmake --build build --target update_translations
```

**注意**：Windows/MSVC 多配置生成器下 `ctest` 必须加 `-C Debug`。输出目录为项目根目录下的 `Debug/` 和 `Release/`。测试自动使用 `QT_QPA_PLATFORM=offscreen`，适用于无头/CI 环境。

### 前置依赖

- Qt 6.9+（Core / Widgets / Network / Gui / OpenGL / OpenGLWidgets / Concurrent / Svg）
- CMake 3.16+
- 支持 C++20 的编译器（MSVC 2019+ / GCC 10+ / Clang 12+）
- OpenSSL（从 `third_party/openssl/` 缓存使用）
- libjpeg-turbo（从 `third_party/libjpeg-turbo/` 缓存使用）
- nvJPEG + CUDA 12.x（可选，从 `third_party/nvjpeg/` 缓存使用）

### 可选编译选项

```bash
# 启用 Sanitizer（ASan / TSan / UBSan / MSan）
cmake -B build -DENABLE_SANITIZERS=address

# 指定 Qt 路径（如未自动找到）
cmake -B build -DQt6_DIR=/path/to/qt6/lib/cmake/Qt6
```

## 使用说明

### 启动服务器
1. 启动应用程序
2. 点击导航面板「服务器」→「启动服务」
3. 等待客户端连接（默认端口：5921）

### 连接客户端
1. 启动应用程序
2. 点击导航面板「连接」→「新建连接」
3. 输入服务器 IP 地址和端口
4. 输入认证密码
5. 点击「连接」
6. 连接成功后可以：
   - 查看远程桌面画面（OpenGL 渲染）
   - 使用鼠标和键盘进行远程操作
   - 使用工具栏功能（全屏、缩放、截图等）

### 高级功能
- **全屏模式**：按 `F11` 或点击工具栏全屏按钮
- **缩放**：适应窗口 / 实际大小 / 放大 / 缩小
- **明暗主题**：设置 → 主题切换

## 日志与配置

### 日志系统

- **日志路径**：
    - Windows：`%APPDATA%/UltraDesktop/logs/`
    - macOS：`~/Library/Application Support/UltraDesktop/logs/`
- **滚动策略**：按大小滚动（size-based rolling）
- **日志级别**：Debug / Info / Warning / Critical
- **分类体系**：6 棵一级分类树（`app` / `core.*` / `server.*` / `client.*` / `ui.*` / `test.*`）
- **默认配置**：最大日志文件 10MB，最大滚动文件数 5，保留 7 天

### 配置文件

- **配置路径**：与日志目录相同
- **支持的配置项**：网络参数（端口、超时、重连间隔）、屏幕捕获参数（帧率、质量、区域）、队列参数（容量、超时）、日志参数（级别、大小、保留策略）

## 测试

项目包含 23 个测试目标，位于 `test/` 目录，镜像 `src/` 结构按模块组织：

```
test/
├── app/                          # 应用壳层测试
├── client/
│   ├── decode/                   # GPU 解码 + GL 上传格式
│   ├── network/                  # 网络冒烟测试
│   ├── session/                  # SessionManager（32 个用例）
│   └── windows/                  # ClientRemoteWindow + 三缓冲 + 延迟指标
├── server/
│   ├── capture/                  # ScreenCapture + DXGI 捕获
│   ├── clienthandler/            # ClientHandler 测试
│   ├── dataflow/                 # QueueManager + 捕获帧 + 数据一致性
│   └── dataprocessing/           # DataProcessing 测试
├── common/
│   └── threading/                # ThreadManager 测试
└── integration/                  # 跨模块集成 + 帧传输延迟
```

测试定义使用 `add_rd_test()` 辅助函数，单个测试定义约 8 行。

### 测试分类

**单元测试** (`-L unit`)：
- `test_threadmanager`：线程管理器测试
- `test_queuemanager`：队列管理器测试
- `test_triple_buffer_swap`：三缓冲交换测试
- `test_captured_frame`：捕获帧测试
- `test_sessionmanager_lifecycle`、`test_sessionmanager_logic`、`test_sessionmanager_data`：SessionManager 测试（32 个用例）

**集成测试** (`-L integration`)：
- `test_producer_consumer_integration`：生产者-消费者模式集成测试
- `test_screen_data_flow`：完整屏幕数据流测试
- `test_screen_capture_integration`：屏幕捕获集成测试

**性能测试** (`-L performance`)：
- `test_frame_transmission_latency`：帧传输延迟测试
- `test_refresh_latency_metric`：刷新延迟指标测试

## 故障排除

### 编译问题
- **Qt6 找不到**：`cmake -B build -DQt6_DIR=/path/to/qt6/lib/cmake/Qt6`
- **OpenSSL 缺失**：确保 `third_party/openssl/` 目录完整（需通过 vcpkg 获取并设置 `VCPKG_ROOT`）
- **C++20 标准不支持**：确保编译器版本（Clang 12+、GCC 10+、MSVC 2019+）

### 运行问题
- **连接失败**：检查服务器是否已启动监听、确认端口未被占用、检查防火墙设置
- **性能问题**：降低帧率（设置 → 屏幕捕获 → 帧率）、减小捕获质量、检查网络带宽
- **应用崩溃**：查看日志文件最后几行、尝试 Debug 版本运行获取更多信息
- **nvJPEG 不可用**：确保 `third_party/nvjpeg/bin/Windows-x64/nvjpeg64_12.dll` 存在且 CUDA GPU 可用

### 调试技巧
- **启用详细日志**：`export QT_LOGGING_RULES="*.debug=true"`（或设置环境变量）
- **查看队列统计**：日志中搜索 `lcServerQueue` 类别
- **性能分析**：使用 Qt Creator 的 Profiler 工具
- **内存检查**（Linux）：`valgrind --leak-check=full ./UltraDesktop`

## 技术文档

- **`docs/project_analysis.md`**：项目分析报告（架构、问题修复状态、优化历史）

## 贡献指南

### 开发流程
1. Fork 本仓库
2. 创建特性分支（`git checkout -b feature/AmazingFeature`）
3. 提交更改（`git commit -m 'Add some AmazingFeature'`）
4. 推送到分支（`git push origin feature/AmazingFeature`）
5. 提交 Pull Request

### 代码规范
- 遵循 Qt 编码风格
- 使用 C++20 标准特性
- 日志必须使用分类流式宏（`qCInfo(lcServer) << "msg"`）
- 添加必要的注释和文档
- 确保所有测试通过（`ctest -C Debug --output-on-failure`）

### 报告问题
提交 Issue 时请包含：问题描述和复现步骤、运行环境、相关日志输出、截图（如适用）

## 许可证

本项目采用 MIT 许可证。详见 LICENSE 文件。

## 联系方式

- **项目主页**：https://github.com/JiaYongChen/UltraDesktop
- **问题反馈**：https://github.com/JiaYongChen/UltraDesktop/issues

## 致谢

感谢以下开源项目和技术：
- **Qt Framework**：强大的跨平台应用框架
- **libjpeg-turbo**：高性能 JPEG 编解码库
- **NVIDIA nvJPEG**：GPU 加速 JPEG 解码
- **OpenSSL**：安全传输层实现
