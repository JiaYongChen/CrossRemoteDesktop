# Qt Remote Desktop

一个基于 Qt 6.9.3 的高性能跨平台远程桌面应用程序，支持 macOS、Windows 与 Linux 之间的远程连接与控制。

## 项目概述

本项目采用现代化的多线程架构和生产者-消费者模式，提供安全、高效、低延迟的远程访问能力。当前已实现屏幕共享、远程输入控制、高性能数据流处理、完善的日志系统等核心功能，其它高级能力按阶段逐步完善。

## 功能特性

### 已实现 ✅
-   **核心功能**
    -   实时屏幕共享（基于队列的高性能数据流）
    -   远程控制（鼠标 + 键盘：点击、滚轮、组合键）
    -   **光标管理**：支持本地光标自动隐藏与远程光标渲染，提供流畅的操作体验
    -   多线程 Worker 架构（ScreenCaptureWorker、DataProcessingWorker、ClientHandlerWorker）
    -   TCP 连接、心跳与智能重连逻辑
    
-   **架构特性**
    -   生产者-消费者模式（ThreadSafeQueue 保证线程安全）
    -   队列管理系统（QueueManager 统一管理捕获队列和处理队列）
    -   分布式拉取架构（客户端按需从队列拉取数据，避免集中式推送瓶颈）
    -   异步数据处理（所有 I/O 操作非阻塞，性能提升 30-3000 倍）
    -   **渲染优化**：独立的 `RenderManager` 处理图像绘制，支持 QImage 优化与脏矩形渲染（规划中）
    
-   **系统功能**
    -   多语言资源（zh_CN / en_US），基础样式与图标资源
    -   完善的日志系统（按文件大小滚动，支持多级别日志，LoggingCategories 分类管理）
    -   灵活的配置系统（支持运行时配置更新）
    -   全面的单元测试（21+ 测试文件，覆盖核心功能）
    -   **输入模拟**：模块化的输入模拟器，支持 Windows/macOS/Linux 的键盘鼠标事件注入
    -   **加密支持**：内置 `Encryption` 模块，为安全传输做准备

### 规划中 ⏳
-   文件传输（双向、断点续传）- FileTransferManager 已预置
-   剪贴板同步（文本/图片）- ClipboardManager 已预置
-   音频传输（捕获/播放，编解码）
-   UDP 渠道与更低延迟的视频编码（H.264 等）
-   多显示器支持
-   TLS/SSL 加密传输

## 系统要求

-   操作系统：
    -   macOS 10.15+（Catalina 及以上，支持 Apple Silicon 和 Intel）
    -   Windows 10 及以上
    -   Linux（实验性支持）
-   内存：4GB 及以上（推荐 8GB）
-   存储：100MB 可用空间
-   网络：TCP/IP 网络环境

## 技术栈

-   **框架**: Qt 6.9.3（Core / Widgets / Network / Multimedia / Gui / OpenGL / OpenGLWidgets / Test）
-   **语言**: C++20
-   **构建**: CMake 3.16+（支持跨平台自动检测）
-   **架构模式**: 
    -   多线程 Worker 模式（Worker 基类 + ThreadManager）
    -   生产者-消费者模式（ThreadSafeQueue）
    -   单例模式（QueueManager）
-   **第三方依赖**: OpenSSL、zlib（用于 PNG 图像编码）
-   **测试框架**: Qt Test（21+ 测试文件）

## 代码结构

```
QtRemoteDesktop/
├── CMakeLists.txt                  # 主构建配置（跨平台自动检测）
├── resources/                      # 资源文件
│   ├── Info.plist.in              # macOS 应用信息模板
│   ├── icons/                     # 应用图标
│   ├── styles/                    # 样式文件
│   ├── resources.qrc              # Qt 资源配置
│   └── translations/              # 多语言翻译文件
├── src/                           # 源代码
│   ├── main.cpp                   # 程序入口
│   ├── client/                    # 客户端模块
│   │   ├── ClientManager.*        # 客户端总控
│   │   ├── managers/              # 业务逻辑管理
│   │   │   ├── SessionManager.*   # 会话管理
│   │   │   └── FileTransferManager.* # 文件传输（预置）
│   │   ├── network/               # 网络通信
│   │   │   ├── ConnectionManager.* # 连接管理
│   │   │   └── TcpClient.*        # TCP 客户端实现
│   │   └── window/                # 界面与渲染
│   │       ├── ClientRemoteWindow.* # 远程桌面显示窗口
│   │       ├── RenderManager.*    # 渲染管理器
│   │       └── CursorManager.*    # 光标管理器
│   ├── server/                    # 服务器模块
│   │   ├── ServerManager.*        # 服务器总管理器
│   │   ├── capture/               # 屏幕捕获
│   │   │   ├── ScreenCapture.*    # 屏幕捕获管理
│   │   │   ├── ScreenCaptureWorker.* # 捕获工作线程
│   │   │   └── CaptureConfig.h    # 捕获配置
│   │   ├── clienthandler/         # 客户端连接处理
│   │   │   └── ClientHandlerWorker.* # 客户端处理 Worker
│   │   ├── dataflow/              # 数据流管理（核心架构）
│   │   │   ├── QueueManager.*     # 队列管理器（单例）
│   │   │   └── DataFlowStructures.* # 数据结构定义
│   │   ├── dataprocessing/        # 数据处理
│   │   │   ├── DataProcessing.*   # 数据处理逻辑
│   │   │   ├── DataProcessingWorker.* # 处理工作线程
│   │   │   └── DataProcessingConfig.* # 处理配置
│   │   ├── service/               # 服务层
│   │   │   ├── ServerWorker.*     # 服务器工作线程
│   │   │   └── TcpServer.*        # TCP 服务器
│   │   └── simulator/             # 输入模拟（跨平台）
│   │       ├── InputSimulator.*   # 输入模拟基类
│   │       ├── KeyboardSimulator*.* # 键盘模拟（Windows/macOS/Linux）
│   │       └── MouseSimulator*.*  # 鼠标模拟（Windows/macOS/Linux）
│   ├── common/                    # 公共模块
│   │   ├── core/                  # 核心功能
│   │   │   ├── threading/         # 线程管理
│   │   │   │   ├── Worker.*       # Worker 基类
│   │   │   │   ├── ThreadManager.* # 线程管理器
│   │   │   │   └── ThreadSafeQueue.h # 线程安全队列
│   │   │   ├── logging/           # 日志系统
│   │   │   │   ├── DebugLogger.*  # 调试日志器
│   │   │   │   └── LoggingCategories.* # 日志分类
│   │   │   ├── config/            # 配置管理
│   │   │   │   ├── Config.*       # 配置管理器
│   │   │   │   ├── Constants.*    # 常量定义
│   │   │   │   ├── NetworkConstants.h # 网络常量
│   │   │   │   ├── MessageConstants.h # 消息常量
│   │   │   │   └── UiConstants.h  # UI 常量
│   │   │   ├── network/           # 网络协议
│   │   │   │   └── Protocol.h     # 协议定义
│   │   │   └── crypto/            # 加密模块
│   │   │       └── Encryption.*   # 加密实现
│   │   ├── data/                  # 数据结构
│   │   │   └── DataRecord.h       # 数据记录
│   │   ├── types/                 # 类型定义
│   │   │   ├── CommonTypes.h      # 通用类型
│   │   │   └── NetworkTypes.h     # 网络类型
│   │   ├── clipboard/             # 剪贴板相关
│   │   │   └── ClipboardManager.* # 剪贴板管理（预置）
│   │   └── windows/               # 窗口类实现
│   │       ├── MainWindow.*       # 主窗口
│   │       ├── ConnectionDialog.* # 连接对话框
│   │       └── SettingsDialog.*   # 设置对话框
│   └── ui/                        # UI 界面文件 (*.ui)
│       ├── mainwindow.ui
│       ├── connectiondialog.ui
│       └── settingsdialog.ui
├── test/                          # 测试套件（21 个测试文件）
│   ├── CMakeLists.txt
│   ├── test_screencaptureworker.cpp       # 屏幕捕获 Worker 测试
│   ├── test_screencapture.cpp             # 屏幕捕获集成测试
│   ├── test_screencapture_config.cpp      # 捕获配置测试
│   ├── test_screencapture_optimization.cpp # 捕获优化测试
│   ├── test_dataprocessing.cpp            # 数据处理测试
│   ├── test_clienthandlerworker.cpp       # 客户端处理 Worker 测试
│   ├── test_clientremotewindow.cpp        # 远程窗口测试
│   ├── test_producer_consumer_integration.cpp  # 生产者-消费者集成测试
│   ├── test_screen_data_flow.cpp          # 屏幕数据流测试
│   ├── test_screen_data_transmission.cpp  # 数据传输测试
│   ├── test_threadmanager.cpp             # 线程管理器测试
│   ├── test_communication.cpp             # 通信测试
│   ├── test_data_consistency.cpp          # 数据一致性测试
│   ├── test_datacleaning_integration.cpp  # 数据清理集成测试
│   ├── test_frame_transmission_latency.cpp # 帧传输延迟测试
│   ├── test_image_display.cpp             # 图像显示测试
│   ├── test_image_transmission_integration.cpp # 图像传输集成测试
│   ├── test_image_type_specification.cpp  # 图像类型测试
│   ├── test_large_message_chunked.cpp     # 大消息分块测试
│   ├── test_screen_capture_integration.cpp # 屏幕捕获集成测试
│   └── test_close_event.cpp               # 关闭事件测试
└── docs/                          # 技术文档（14 个文档）
    ├── architecture_improvement_recommendations.md # 架构改进建议
    ├── threading_architecture.md          # 线程架构文档
    ├── threading_implementation_summary.md # 线程实现总结
    ├── threading_limitations.md           # 线程限制说明
    ├── local_cursor_hiding_feature.md     # 本地光标隐藏特性
    ├── remote_cursor_display_feature.md   # 远程光标显示特性
    ├── qimage_optimization.md             # QImage 优化文档
    ├── input_simulator_refactoring.md     # 输入模拟重构文档
    ├── keyboard_mapping_analysis.md       # 键盘映射分析
    ├── keyboard_modifier_fix.md           # 键盘修饰键修复
    ├── keypad_backspace_fix.md            # 小键盘退格修复
    ├── redundancy_analysis.md             # 冗余分析
    ├── redundancy_cleanup_summary.md      # 冗余清理总结
    └── ConnectionInstance_destruction_optimization.md # 连接实例销毁优化
```

### 核心架构说明

**数据流架构**（生产者-消费者模式）：
```
ScreenCaptureWorker → m_captureQueue → DataProcessingWorker → m_processedQueue → ClientHandlerWorker
    (生产者)              (120帧容量)         (消费者+生产者)        (120帧容量)         (消费者)
```

**关键特性**：
- **队列驱动**: 所有数据传输通过 ThreadSafeQueue，避免信号槽开销
- **分布式拉取**: 每个客户端独立从队列拉取数据（`sendScreenDataFromQueue()`）
- **异步处理**: 所有 I/O 操作使用 `Qt::QueuedConnection` 非阻塞执行
- **线程安全**: QMutex 保护所有共享资源访问

## 性能优化亮点

### 异步架构改进
- **processTask() 性能**: 从 0.6-60ms 优化至 ~20μs（**30-3000倍提升**）
- **非阻塞 I/O**: 所有网络和数据操作使用 `QMetaObject::invokeMethod` 异步执行
- **零拷贝优化**: 使用移动语义和智能指针减少内存拷贝

### 队列系统优化
- **无信号开销**: 彻底移除 `frameCaptured` 和 `frameReady` 信号，改用纯队列通信
- **容量管理**: 动态队列容量（默认 120 帧），支持运行时调整
- **统计监控**: 实时队列性能统计（入队/出队速率、延迟、利用率）

### 内存管理
- **帧复用**: 避免频繁的图像内存分配
- **智能指针**: 使用 `std::unique_ptr` 和 `std::shared_ptr` 自动管理资源
- **队列清理**: 定期清理过期帧数据，防止内存泄漏

## 编译与构建

严格遵循“禁止在项目根目录直接运行 CMake”，仅在根目录下创建 build 子目录进行构建。

### macOS（推荐 Homebrew）

```bash
# 安装 Xcode 命令行工具
xcode-select --install

# 安装依赖
brew install qt@6 cmake openssl zlib

# 构建
mkdir -p build && cd build
cmake ..
cmake --build . --config Release -j8

# 运行（macOS）
./bin/QtRemoteDesktop.app/Contents/MacOS/QtRemoteDesktop
```

### Windows

1. 安装 Qt 6.9.1、CMake、OpenSSL、并准备 VS 2019+ 或 MinGW
2. 配置并构建：

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release -j8

# 运行（Windows）
..\bin\QtRemoteDesktop.exe
```

### 部署（macOS 阶段 E）

在构建目录中执行：

```bash
cmake --build . --target deploy_macos -j8
# 应用包示例路径： ./bin/QtRemoteDesktop.app
```

deploy_macos 目标会在打包前清理历史 Frameworks 目录并调用 macdeployqt 完成依赖部署。

### 可选编译选项

```bash
# 调试版本
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 指定 Qt 路径（如未自动找到）
cmake -DQt6_DIR=/path/to/qt6/lib/cmake/Qt6 ..
```

## 使用说明

### 启动服务器
1. 启动应用程序
2. 选择「服务器」菜单 → 「启动服务」
3. 等待客户端连接（默认端口：通过配置文件指定）

### 连接客户端
1. 启动应用程序
2. 选择「连接」→ 「新建连接」
3. 输入服务器 IP 地址和端口
4. 点击「连接」
5. 连接成功后，可以：
   - 查看远程桌面画面
   - 使用鼠标和键盘进行远程操作
   - 使用工具栏功能（全屏、截图、设置等）

### 高级功能
- **全屏模式**: 按 `F11` 或点击工具栏全屏按钮
- **性能调整**: 设置 → 调整帧率、质量参数
- **日志查看**: 查看应用程序日志目录（见下方路径）

## 日志与配置

### 日志系统
-   **日志路径**：
    -   macOS：`~/Library/Application Support/QtRemoteDesktop/logs/`
    -   Windows：`%APPDATA%/QtRemoteDesktop/logs/`
-   **滚动策略**：按大小滚动（size-based rolling）
-   **日志级别**：Debug / Info / Warning / Critical
-   **默认配置**：
    -   最大日志文件大小：10MB
    -   最大滚动文件数：5
    -   日志保留天数：7 天

### 配置文件
-   **配置路径**：与日志目录相同
-   **支持的配置项**：
    -   网络参数（端口、超时、重连间隔）
    -   屏幕捕获参数（帧率、质量、区域）
    -   队列参数（容量、超时）
    -   日志参数（级别、大小、保留策略）

### 性能监控
应用程序提供实时性能统计：
- **捕获队列统计**：入队速率、队列大小、平均延迟
- **处理队列统计**：处理速率、压缩率、数据吞吐量
- **Worker 性能**：线程状态、任务耗时、错误率

## 测试

项目包含完善的测试套件，位于 `test/` 目录，通过主工程 CMake `add_subdirectory(test)` 自动构建。

### 测试覆盖（21 个测试文件）

**核心组件测试**:
- `test_screencaptureworker`: 屏幕捕获 Worker 测试
- `test_screencapture`: 屏幕捕获管理测试
- `test_screencapture_config`: 捕获配置测试
- `test_screencapture_optimization`: 捕获优化测试
- `test_dataprocessing`: 数据处理测试
- `test_clienthandlerworker`: 客户端处理 Worker 测试
- `test_clientremotewindow`: 远程窗口测试
- `test_threadmanager`: 线程管理器测试

**集成测试**:
- `test_producer_consumer_integration`: 生产者-消费者模式集成测试
- `test_screen_data_flow`: 完整屏幕数据流测试
- `test_screen_data_transmission`: 数据传输测试
- `test_screen_capture_integration`: 屏幕捕获集成测试
- `test_image_transmission_integration`: 图像传输集成测试
- `test_datacleaning_integration`: 数据清理集成测试

**功能测试**:
- `test_communication`: 通信功能测试
- `test_data_consistency`: 数据一致性测试
- `test_image_display`: 图像显示测试
- `test_image_type_specification`: 图像类型规范测试
- `test_large_message_chunked`: 大消息分块测试
- `test_close_event`: 关闭事件测试

**性能测试**:
- `test_frame_transmission_latency`: 帧传输延迟测试

### 运行测试

**运行全部测试**（在 `build/` 或 `debug/` 目录）：
```bash
ctest --output-on-failure
```

**运行单个测试**：
```bash
./build/test/test_screencaptureworker
./build/test/test_producer_consumer_integration
```

**测试结果示例**：
```
********* Start testing of TestScreenCaptureWorker *********
Totals: 14 passed, 0 failed, 0 skipped, 0 blacklisted, 838ms
********* Finished testing of TestScreenCaptureWorker *********
```

## 故障排除

### 编译问题
-   **Qt6 找不到**：
    ```bash
    cmake -DQt6_DIR=/path/to/qt6/lib/cmake/Qt6 ..
    ```
-   **OpenSSL 缺失**：
    ```bash
    # macOS
    brew install openssl
    # Windows
    # 从 https://slproweb.com/products/Win32OpenSSL.html 下载安装
    ```
-   **C++20 标准不支持**：确保编译器版本（Clang 12+, GCC 10+, MSVC 2019+）

### 运行问题
-   **连接失败**：
    - 检查服务器是否已启动监听
    - 确认端口未被占用（`netstat -an | grep <port>`）
    - 检查防火墙设置
    - 验证网络连通性（`ping` 或 `telnet`）

-   **性能问题**：
    - 降低帧率（设置 → 屏幕捕获 → 帧率）
    - 减小捕获质量（设置 → 图像质量）
    - 检查网络带宽
    - 查看日志中的队列警告信息

-   **应用崩溃**：
    - 查看日志文件最后几行
    - 检查是否有内存不足
    - 验证 Qt 库版本匹配
    - 尝试 Debug 版本运行获取更多信息

### 调试技巧
-   **启用详细日志**：设置环境变量
    ```bash
    export QT_LOGGING_RULES="*.debug=true"
    ```
-   **查看队列统计**：日志中搜索 "queuemanager" 类别
-   **性能分析**：使用 Qt Creator 的 Profiler 工具
-   **内存检查**：
    ```bash
    # macOS
    leaks QtRemoteDesktop
    # Linux
    valgrind --leak-check=full ./QtRemoteDesktop
    ```

## 技术文档

项目包含详细的架构设计和实施文档，位于 `docs/` 目录（共 14 个文档）：

### 架构文档
-   **`architecture_improvement_recommendations.md`**: 架构改进建议
-   **`threading_architecture.md`**: 线程架构文档
-   **`threading_implementation_summary.md`**: 线程实现总结
-   **`threading_limitations.md`**: 线程限制说明
-   **`redundancy_analysis.md`**: 冗余分析文档
-   **`redundancy_cleanup_summary.md`**: 冗余清理总结

### 实施文档
-   **`local_cursor_hiding_feature.md`**: 本地光标隐藏特性
-   **`remote_cursor_display_feature.md`**: 远程光标显示特性
-   **`qimage_optimization.md`**: QImage 优化文档
-   **`input_simulator_refactoring.md`**: 输入模拟重构文档
-   **`ConnectionInstance_destruction_optimization.md`**: 连接实例销毁优化

### 键盘输入文档
-   **`keyboard_mapping_analysis.md`**: 键盘映射分析
-   **`keyboard_modifier_fix.md`**: 键盘修饰键修复
-   **`keypad_backspace_fix.md`**: 小键盘退格修复

## 贡献指南

欢迎贡献代码、报告问题或提出改进建议！

### 开发流程
1. Fork 本仓库
2. 创建特性分支（`git checkout -b feature/AmazingFeature`）
3. 提交更改（`git commit -m 'Add some AmazingFeature'`）
4. 推送到分支（`git push origin feature/AmazingFeature`）
5. 提交 Pull Request

### 代码规范
-   遵循 Qt 编码风格
-   使用 C++20 标准特性
-   添加必要的注释和文档
-   确保所有测试通过（`ctest --output-on-failure`）
-   更新相关文档

### 报告问题
提交 Issue 时请包含：
-   问题描述和复现步骤
-   运行环境（操作系统、Qt 版本、编译器）
-   相关日志输出
-   截图（如适用）

## 许可证

本项目采用 MIT 许可证。详见 LICENSE 文件。

## 联系方式

-   **项目主页**: https://github.com/JiaYongChen/QtRemoteDesktop
-   **问题反馈**: https://github.com/JiaYongChen/QtRemoteDesktop/issues
-   **技术讨论**: 欢迎在 Issues 中提问和讨论

## 致谢

感谢以下开源项目和技术：
-   **Qt Framework**: 强大的跨平台应用框架
-   **OpenSSL**: 安全传输层实现
-   **zlib**: 高效的数据压缩库

特别感谢所有贡献者和测试用户的支持！

---

## 版本历史

### v1.0.0 (当前版本)
-   ✅ 完整的屏幕捕获和远程控制功能
-   ✅ 基于队列的高性能数据流架构
-   ✅ 多线程 Worker 模式（Worker 基类 + ThreadManager）
-   ✅ 完善的日志和配置系统
-   ✅ 跨平台输入模拟（Windows/macOS/Linux）
-   ✅ 21+ 测试文件，覆盖核心功能
-   ✅ 14 个技术文档，详细记录架构设计

### 计划中的版本
-   **v1.1.0**: 文件传输（FileTransferManager 已预置）和剪贴板同步（ClipboardManager 已预置）
-   **v1.2.0**: 音频传输支持
-   **v2.0.0**: H.264 视频编码、UDP 传输、TLS 加密（Encryption 模块已预置）

---

**注意**: 本项目仅供学习和研究使用，请遵守相关法律法规。在使用远程桌面功能时，请确保获得了目标计算机所有者的明确授权。