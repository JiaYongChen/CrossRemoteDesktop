# 平台支持

状态说明：✅ 表示已接入当前构建并可作为支持路径；⚠️ 表示已接入但尚未完成目标平台验证；🚫 表示主动禁用；⛔ 表示当前平台构建被阻塞；— 表示不适用。

## 实现状态矩阵

| 组件 | Windows | Linux | macOS |
|------|:-------:|:-----:|:-----:|
| **构建基线** | ✅ x64 缓存完整 | ✅ x64 已验证 | ⛔ 第三方缓存不完整 |
| **屏幕捕获** |  |  |  |
| DXGI Desktop Duplication | ✅ | — | — |
| PipeWire | — | 🚫 当前禁用 | — |
| ScreenCaptureKit (`AvFoundationCapture`) | — | — | ⚠️ 已接入，待验证 |
| Qt QScreen (`QScreen::grabWindow`) | ✅ 自动回退 | ✅ 当前基线 | ⚠️ 已接入回退，待验证 |
| **JPEG 解码** |  |  |  |
| NVIDIA NVJPEG (CUDA) | ✅ | — | — |
| VA-API | — | 🚫 当前禁用 | — |
| `MacOSImageDecoder` (ImageIO/CoreGraphics) | — | — | ⚠️ 已接入，待验证 |
| TurboJPEG (libjpeg-turbo) | ✅ | ✅ | ⚠️ 缓存不完整 |
| **输入模拟** |  |  |  |
| 键盘 | ✅ SendInput | ✅ X11/XTest | ⚠️ CGEvent 源码存在，未整体验证 |
| 鼠标 | ✅ SendInput | ✅ X11/XTest | ⚠️ CGEvent 源码存在，未整体验证 |
| **其他** |  |  |  |
| 开机自启 | 注册表 | autostart .desktop | LaunchAgent plist |
| TLS 证书生成 | ✅ | ✅ | ✅ |

## 回退路径

### Windows
- 捕获：DXGI 初始化失败时回退 Qt QScreen
- 解码：NVJPEG → TurboJPEG CPU

### Linux
- 当前捕获：Qt QScreen（`QScreen::grabWindow`）
- 当前解码：TurboJPEG CPU
- PipeWire 与 VA-API 当前显式禁用，不进入运行时选择链；安装开发包也不会改变该基线

### macOS
- 已接入捕获链：ScreenCaptureKit → Qt QScreen
- 已接入解码链：ImageIO/CoreGraphics → TurboJPEG CPU
- 第三方缓存仍阻止完整 macOS 构建，因此上述链路尚未经过目标平台编译和真机运行验证

## 已知限制

### macOS
- `AvFoundationCapture.mm` 和 `MacOSImageDecoder.mm` 已纳入 Apple 构建；根 CMake 已启用 `OBJCXX`，并在目标创建后链接 ScreenCaptureKit、ImageIO/CoreGraphics 等框架。
- macOS x64 缓存仍缺少 libjpeg-turbo 和 OpenSSL Debug 库，macOS ARM64 尚未提供 OpenSSL/libjpeg-turbo 平台缓存。补齐缓存前，不能从当前仓库完成 macOS 构建，也不能宣称后端已经真机验证。

### Linux
- PipeWire 尚未实现 xdg-desktop-portal 授权、remote fd、node id 和流生命周期，因此 CMake 不定义 `HAS_PIPEWIRE`，固定使用 Qt QScreen。
- VA-API 尚未建立完整有效的 JPEG 解码上下文，因此 CMake 不定义 `HAS_VAAPI`，固定使用 CPU TurboJPEG。
- Linux 输入模拟仅实现 X11/XTest；原生 Wayland 输入注入尚未实现。

### 通用
- 无双显示器选择 UI（DXGI 底层已支持 outputIndex 参数）
- 无带宽自适应（仅静态 JPEG 质量参数）
