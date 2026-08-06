# 平台支持

## 实现状态矩阵

| 组件 | Windows | Linux | macOS |
|------|:-------:|:-----:|:-----:|
| **屏幕捕获** |
| DXGI Desktop Duplication | ✅ | — | — |
| PipeWire + DMA-BUF | — | ✅ | — |
| ScreenCaptureKit (AvFoundation) | — | — | ⚠️ 仅头文件 |
| Qt GDI 回退 (QScreen::grabWindow) | 自动 | 自动 | ✅ |
| **GPU 解码** |
| NVIDIA NVJPEG (CUDA) | ✅ | — | — |
| VA-API | — | ✅ | — |
| VideoToolbox + CoreGraphics | — | — | ⚠️ 仅头文件 |
| **CPU 解码** |
| TurboJPEG (libjpeg-turbo) | ✅ | ✅ | ✅ |
| **输入模拟** |
| 键盘 | ✅ SendInput | ✅ X11/wayland | ✅ CGEvent |
| 鼠标 | ✅ SendInput | ✅ X11/wayland | ✅ CGEvent |
| **其他** |
| 开机自启 | 注册表 | autostart .desktop | LaunchAgent plist |
| TLS 证书生成 | ✅ | ✅ | ✅ |

## 回退路径

### Windows
- 捕获：始终使用 DXGI（无回退）
- 解码：NVJPEG → TurboJPEG CPU

### Linux
- 捕获：PipeWire（compile-time HAS_PIPEWIRE 且运行时可用时）→ Qt GDI
- 解码：VA-API（运行时探测，不可用时跳过）→ TurboJPEG CPU

### macOS
- 捕获：Qt GDI（AvFoundationCapture 仅头文件，实现待补齐）
- 解码：TurboJPEG CPU（VideoToolboxDecoder 仅头文件，实现待补齐）

## 已知限制

### macOS
- AvFoundationCapture（`src/server/capture/macos/`）：头文件已定义接口，.cpp 实现待开发。当前全部回退到 QScreen::grabWindow()，CPU 密集、低帧率。
- VideoToolboxDecoder（`src/client/decode/macos/`）：头文件已定义接口，.cpp 实现待开发。当前全部回退到 TurboJPEG CPU 解码。

### Linux
- PipeWire 捕获通过 CMake HAS_PIPEWIRE 编译期控制
- VA-API 解码器 GPU 不可用时自动回退

### 通用
- 无双显示器选择 UI（DXGI 底层已支持 outputIndex 参数）
- 无带宽自适应（仅静态 JPEG 质量参数）
