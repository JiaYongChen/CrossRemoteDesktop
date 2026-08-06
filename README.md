# UltraDesktop（极域桌面）

跨平台远程桌面应用 —— 客户端和服务端集成在单个可执行文件中。

## 功能

- **屏幕共享**：实时捕获桌面并通过 JPEG 编码传输
- **远程控制**：键盘/鼠标事件转发，支持仅查看模式
- **硬件加速**：Windows (DXGI + NVJPEG)、Linux (PipeWire + VA-API)
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
| 屏幕捕获 | DXGI (GPU) | PipeWire (GPU) | Qt GDI (CPU) |
| GPU 解码 | NVJPEG | VA-API | — (回退 CPU) |
| CPU 解码 | TurboJPEG | TurboJPEG | TurboJPEG |
| 输入模拟 | ✅ | ✅ | ✅ |

> macOS 捕获（ScreenCaptureKit）和解码（VideoToolbox）的头文件已定义，实现待后续版本补齐。

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

详见 [docs/BUILD.md](docs/BUILD.md)。

## 架构

详见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 变更日志

详见 [docs/CHANGELOG.md](docs/CHANGELOG.md)。
