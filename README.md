# Qt Remote Desktop

一个基于 Qt 6.9.1 的跨平台远程桌面应用程序，支持 macOS 与 Windows 之间的远程连接与控制。

## 项目概述

本项目提供安全、高效的远程访问能力，当前已实现屏幕共享、远程输入控制、日志系统等核心功能，其它高级能力按阶段逐步完善。

## 功能特性

-   已实现
    -   ✅ 实时屏幕共享
    -   ✅ 远程控制（鼠标 + 键盘：点击、滚轮、组合键）
    -   ✅ TCP 连接、心跳与基础重连逻辑
    -   ✅ 多语言资源（zh_CN / en_US），基础样式与图标资源
    -   ✅ 日志系统（按文件大小滚动，最大文件大小/数量可配置）
    -   ✅ 基础配置系统
-   规划中
    -   ⏳ 文件传输（双向、断点续传）
    -   ⏳ 剪贴板同步（文本/图片）
    -   ⏳ 音频传输（捕获/播放，编解码）
    -   ⏳ UDP 渠道与更低延迟的视频编码（H.264 等）
    -   ⏳ 多显示器支持

## 系统要求

-   操作系统：
    -   macOS 10.15+（Catalina 及以上）
    -   Windows 10 及以上
-   内存：4GB 及以上（推荐 8GB）
-   存储：100MB 可用空间
-   网络：TCP/IP 网络环境

## 技术栈

-   框架：Qt 6.9.1（Core / Widgets / Network / Multimedia / Gui / OpenGL / OpenGLWidgets）
-   语言：C++17
-   构建：CMake 3.16+
-   第三方依赖：OpenSSL、zlib（用于PNG图像编码）

## 代码结构（当前工程）

```
QtRemoteDesktop/
├── CMakeLists.txt
├── resources/
│   ├── Info.plist.in
│   ├── icons/
│   ├── resources.qrc
│   └── translations/
├── src/
│   ├── client/
│   │   ├── clientmanager.*
│   │   ├── clientremotewindow.*
│   │   ├── inputhandler.*
│   │   ├── managers/
│   │   │   ├── connectionmanager.*
│   │   │   └── sessionmanager.*
│   │   ├── tcpclient.*
│   ├── common/
│   │   ├── core/（protocol、encryption、logger、config、constants 等）
│   │   └── windows/（mainwindow、dialogs 的 C++ 实现）
│   ├── server/
│   │   ├── clienthandler.*
│   │   ├── inputsimulator.*
│   │   ├── screencapture.*
│   │   ├── servermanager.*
│   │   └── tcpserver.*
│   ├── ui/（.ui 文件：mainwindow.ui、connectiondialog.ui、settingsdialog.ui）
│   └── main.cpp
└── test/
    ├── CMakeLists.txt
    ├── test_connection_manager.cpp
    ├── test_session_manager.cpp
    ├── test_logger.cpp
    ├── test_image_transmission_integration.cpp
    ├── test_screen_data_transmission.cpp
    └── test_producer_consumer_integration.cpp
```

提示：UI 的 .ui 文件在 src/ui 下，具体窗口类的 C++ 实现在 src/common/windows 下。

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

## 使用说明（当前实现）

-   服务器模式：启动应用 -> 服务器菜单启动监听 -> 等待客户端连接
-   客户端模式：新建连接，填写主机与端口，建立连接后显示远程屏幕并可进行鼠标键盘控制
-   视图与操作：全屏、缩放、截图等（具体以 UI 菜单/工具栏为准）

## 日志与配置

-   日志路径：
    -   macOS：~/Library/Application Support/QtRemoteDesktop/logs/
    -   Windows：%APPDATA%/QtRemoteDesktop/logs/
-   滚动策略：按大小滚动（size-based），可配置最大文件大小与最大滚动文件数量
-   默认参数（代码中定义）：
    -   最大日志文件大小：10MB（CoreConstants::DEFAULT_MAX_FILE_SIZE）
    -   最大滚动文件数：5（CoreConstants::DEFAULT_MAX_FILE_COUNT）

## 测试

测试工程位于 test/ 目录，已通过主工程 CMake add_subdirectory(test) 纳入构建。

-   快速运行全部测试（在 build/ 目录）：

```bash
ctest --output-on-failure
```

-   也可单独构建/运行指定测试目标（名称示例）：
    -   test_connection_manager
    -   test_session_manager
    -   test_logger
    -   test_image_transmission_integration / test_screen_data_transmission / test_producer_consumer_integration

## 故障排除

-   无法运行：检查 Qt6 路径、运行库与 OpenSSL、zlib 是否安装到系统标准路径（/opt/homebrew、/usr/local 等）
-   连接失败：检查目标主机监听、端口与防火墙
-   性能瓶颈：调整帧率

## 许可证

本项目采用 MIT 许可证。详见 LICENSE 文件。

## 联系方式

-   项目主页：https://github.com/your-repo/QtRemoteDesktop
-   问题反馈：https://github.com/your-repo/QtRemoteDesktop/issues
-   邮箱：support@qtremotedesktop.com

---

注意：本项目仅供学习和研究使用，请遵守相关法律法规。
