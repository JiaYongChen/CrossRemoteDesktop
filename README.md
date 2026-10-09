# UltraDesktop（极域桌面）

跨平台远程桌面应用 —— 客户端和服务端集成在单个可执行文件中。

## 功能

- **屏幕共享**：实时捕获桌面并通过 JPEG 编码传输
- **远程控制**：键盘/鼠标事件转发，支持仅查看模式
- **平台后端**：Windows 已接通 DXGI + NVJPEG；Linux 使用稳定的 QScreen + TurboJPEG；macOS 后端已接入构建，待缓存补齐后验证
- **安全传输**：TLS 1.3 加密 + PKI 证书 + TOFU（首次信任）模型
- **剪贴板同步**：双向同步文本和图片
- **光标同步**：远程光标形状实时显示
- **全屏模式**：浮动工具栏 + 缩放控制
- **连接历史**：搜索 + 编辑 + 一键重连
- **深色/浅色主题**
- **系统托盘**：最小化到托盘 + 托盘菜单
- **国际化**：中文 / English
- **开机自启**：Windows / macOS / Linux

## 平台支持

| 功能 | Windows | Linux | macOS |
|------|:-------:|:-----:|:-----:|
| 屏幕捕获 | ✅ DXGI → Qt QScreen | ✅ Qt QScreen；PipeWire 已禁用 | ⚠️ ScreenCaptureKit → Qt QScreen，待验证 |
| JPEG 解码 | ✅ NVJPEG → TurboJPEG | ✅ TurboJPEG；VA-API 已禁用 | ⚠️ ImageIO/CoreGraphics → TurboJPEG，待验证 |
| 输入模拟 | ✅ SendInput | ✅ X11/XTest | ⚠️ CGEvent 源码存在，未整体验证 |

> 当前 Windows x64 与 Linux x64 的第三方缓存完整；Linux 已验证的基线使用 Qt QScreen 捕获和 CPU TurboJPEG 解码。macOS 的 Objective-C++、ScreenCaptureKit 与 ImageIO/CoreGraphics 已接入 CMake，但第三方缓存仍不完整，暂不能完成整项目构建或真机验证。详见 [平台支持](docs/PLATFORM_SUPPORT.md) 与 [构建指南](docs/BUILD.md#macos)。

## 快速开始

### 服务端（被控端）

1. 启动应用，点击左侧「启动服务器」
2. 默认监听端口 `5921`（可在设置中修改）
3. 状态栏显示"服务器运行中"即就绪

### 客户端（控制端）

1. 点击「新建连接」，输入服务端 IP 和端口
2. 可选填写用户名和密码（与服务端设置一致）
3. 点击「连接」

## 构建

```bash
# Linux
./build.sh --run-tests
```

```powershell
# Windows PowerShell 5.1 / PowerShell 7
./build.ps1 -run-tests
```

`build.sh` 的命令行与路径处理兼容 macOS/Bash 3.2；当前仍需先补齐对应架构的第三方缓存，才能在 macOS 上完成构建和验证。

详见 [docs/BUILD.md](docs/BUILD.md)。

## 架构

详见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 变更日志

详见 [docs/CHANGELOG.md](docs/CHANGELOG.md)。
