# 架构文档

## 概述

UltraDesktop 是基于 Qt 6 的远程桌面应用，客户端和服务端集成在单个可执行文件中。采用依赖注入（DI）架构，无全局单例。线程管理通过 ThreadManager + Worker 基类实现。

## 目录结构

```
src/
├── app/           应用壳（MainWindow, ConnectionDialog, SettingsDialog）
├── client/        客户端
│   ├── core/         核心数据结构（TripleBuffer, FrameSlot）
│   ├── decode/       解码器（CPU TurboJPEG / GPU NVJPEG / VA-API / VideoToolbox）
│   ├── managers/     解码工作线程
│   ├── network/      网络层（TcpClient, ConnectionManager, ServerTrustStore）
│   ├── session/      会话组装（RemoteDesktopSession, ProtocolSession, DecodePipeline）
│   └── window/       远程桌面窗口（ClientRemoteWindow, GLTextureViewport, CursorManager, ...）
├── common/        共享代码
│   ├── clipboard/    剪贴板管理
│   ├── config/       编译期常量 + 运行时配置 + 连接历史
│   ├── crypto/       密码加解密（AES-256-CBC + PBKDF2）
│   ├── data/         连接参数等共享数据结构（ConnectionParams）
│   ├── error/        统一错误类型 RdError
│   ├── logging/      日志分类（六棵一级分类树）
│   ├── network/      协议定义 + 消息编解码
│   ├── platform/     平台特定（AutoStartManager）
│   ├── theme/        图标加载 + 标题栏主题
│   └── threading/    线程管理 + 线程安全队列 + Worker 基类
├── server/        服务端
│   ├── capture/      屏幕捕获（DXGI/PipeWire/AvFoundation 工厂）
│   ├── clienthandler/ 客户端连接处理 + 认证
│   ├── dataflow/     数据流结构 + 队列管理
│   ├── dataprocessing/ JPEG 编码工作线程
│   ├── listener/     TCP 监听器
│   ├── service/      服务编排门面
│   ├── session/      每客户端独立会话
│   └── simulator/    键盘/鼠标输入模拟（三平台）
└── ui/            Qt Designer .ui 文件
```

## 依赖注入链

```
MainWindow
  ├── ThreadManager（线程生命周期管理）
  ├── QueueManager（捕获队列，Drain-to-Latest 语义）
  ├── ServerService（服务端门面）
  │     ├── TcpListener（TCP 监听 + Worker 线程）
  │     ├── CapturePipeline（屏幕捕获 + 帧广播）
  │     │     ├── ScreenCapture（平台工厂）
  │     │     └── FrameBroadcaster（订阅/广播）
  │     └── ServerSession × N（每客户端独立会话）
  │           ├── SessionQueuePair（私有队列对）
  │           ├── DataProcessingWorker（JPEG 编码线程）
  │           └── ClientHandlerWorker（网络发送线程）
  └── RemoteDesktopSession × N（客户端会话）
        ├── ConnectionManager（内部 TcpClient + TOFU 证书管理）
        ├── ProtocolSession（协议编解码 + 消息路由）
        ├── DecodePipeline（DecodeWorker + TripleBuffer）
        └── ClientRemoteWindow（QWidget + GL 视口）
```

## 数据管线（生产者-消费者）

### 服务端管线

ScreenCapture → QueueManager(captureQueue) → FrameBroadcaster → [广播到各 Session] → SessionQueuePair.captureQueue → DataProcessingWorker（JPEG 编码）→ SessionQueuePair.processedQueue → ClientHandlerWorker（TCP 发送）

### 客户端管线

TcpClient 接收 → ProtocolSession 解码帧 → DecodePipeline → DecodeWorker（JPEG 解码到 TripleBuffer）→ GLTextureViewport（OpenGL 纹理渲染）

## 线程模型

所有工作线程继承 Worker 基类（`src/common/threading/Worker.h`）：
- 状态机：Stopped → Starting → Running ⇄ Paused → Stopping → Stopped
- 生命周期：initialize() → processTask() loop → cleanup()
- 统一由 ThreadManager 管理

主要线程：Main 线程（GUI）、TcpListener 线程、ScreenCapture 线程、DataProcessingWorker 线程、ClientHandlerWorker 线程、DecodeWorker 线程

## 网络协议（RDCP）

- 魔数：0x52444350，24 字节头部
- 校验：CRC-32
- 默认端口：5921
- 传输加密：TLS 1.3

消息类型：
- 连接认证 (0x00xx)：VERSION_EXCHANGE, AUTHENTICATION_REQUEST/RESPONSE, ENCODE_PREFS
- 屏幕数据 (0x10xx)：SCREEN_DATA, CURSOR_SHAPE
- 输入事件 (0x20xx)：MOUSE_EVENT, KEYBOARD_EVENT
- 剪贴板 (0x30xx)：CLIPBOARD_DATA
- 心跳 (0xF0xx)：HEARTBEAT/RESPONSE

## 错误处理

统一错误类型 RdError（code + message + source + timestampMs），所有错误信号使用 const RdError& 参数。协议预期结果通过语义化信号表达，不使用 ErrorCode。
