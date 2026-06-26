#include "LoggingCategories.h"

// ============================================================================
// 核心模块日志分类定义
// ============================================================================

/// 应用程序主模块日志
Q_LOGGING_CATEGORY(lcApp, "app", QtDebugMsg)

/// 协议处理模块日志
Q_LOGGING_CATEGORY(lcProtocol, "core.protocol", QtDebugMsg)

// ============================================================================
// 服务端模块日志分类定义
// ============================================================================

/// 服务端主模块日志
Q_LOGGING_CATEGORY(lcServer, "server", QtDebugMsg)

/// 服务端管理器日志
Q_LOGGING_CATEGORY(lcServerManager, "server.manager", QtDebugMsg)

/// 服务端网络模块日志
Q_LOGGING_CATEGORY(lcNetServer, "server.net", QtDebugMsg)

/// 输入模拟器日志
Q_LOGGING_CATEGORY(lcInputSimulator, "server.inputsimulator", QtDebugMsg)

/// 客户端处理器Worker日志
Q_LOGGING_CATEGORY(lcClientHandlerWorker, "server.clienthandler", QtDebugMsg)

/// 屏幕捕获管理器日志
Q_LOGGING_CATEGORY(lcScreenCaptureManager, "server.capture.manager", QtDebugMsg)

/// 屏幕捕获Worker日志
Q_LOGGING_CATEGORY(lcScreenCaptureWorker, "server.capture.worker", QtDebugMsg)

/// DXGI Desktop Duplication 捕获引擎日志
Q_LOGGING_CATEGORY(lcDxgiCapture, "server.capture.dxgi", QtDebugMsg)

/// 队列管理器日志
Q_LOGGING_CATEGORY(lcQueueManager, "queuemanager", QtDebugMsg)

/// 数据处理Worker日志
Q_LOGGING_CATEGORY(lcDataProcessingWorker, "dataprocessingworker", QtDebugMsg)

/// 数据处理模块日志
Q_LOGGING_CATEGORY(lcDataProcessing, "dataprocessing", QtDebugMsg)

/// 数据处理配置日志
Q_LOGGING_CATEGORY(lcDataProcessingConfig, "dataprocessing.config", QtDebugMsg)

/// 键盘模拟器日志(Linux)
Q_LOGGING_CATEGORY(lcKeyboardSimulatorLinux, "simulator.keyboard.linux", QtDebugMsg)

/// 键盘模拟器日志(macOS)
Q_LOGGING_CATEGORY(lcKeyboardSimulatorMacOS, "simulator.keyboard.macos", QtDebugMsg)

/// 键盘模拟器日志(Windows)
Q_LOGGING_CATEGORY(lcKeyboardSimulatorWindows, "simulator.keyboard.windows", QtDebugMsg)

/// 鼠标模拟器日志(Linux)
Q_LOGGING_CATEGORY(lcMouseSimulatorLinux, "simulator.mouse.linux", QtDebugMsg)

/// 鼠标模拟器日志(macOS)
Q_LOGGING_CATEGORY(lcMouseSimulatorMacOS, "simulator.mouse.macos", QtDebugMsg)

/// 鼠标模拟器日志(Windows)
Q_LOGGING_CATEGORY(lcMouseSimulatorWindows, "simulator.mouse.windows", QtDebugMsg)

// ============================================================================
// 客户端模块日志分类定义
// ============================================================================

/// 客户端主模块日志
Q_LOGGING_CATEGORY(lcClient, "client", QtDebugMsg)

/// 客户端远程窗口日志
Q_LOGGING_CATEGORY(lcClientRemoteWindow, "client.remotewindow", QtDebugMsg)

/// 会话模块日志（新架构：DecodePipeline / ProtocolSession / RemoteDesktopSession）
Q_LOGGING_CATEGORY(lcSession, "client.session", QtDebugMsg)

/// 解码管线日志（帧解码）
Q_LOGGING_CATEGORY(lcDecodePipeline, "client.pipeline", QtDebugMsg)

/// OpenGL纹理视口日志
Q_LOGGING_CATEGORY(lcGLViewport, "client.glviewport", QtDebugMsg)

/// 客户端刷新性能度量日志
Q_LOGGING_CATEGORY(lcRefreshMetrics, "qrd.client.refresh.metrics", QtDebugMsg)

// ============================================================================
// 用户界面模块日志分类定义
// ============================================================================

/// UI主模块日志
Q_LOGGING_CATEGORY(lcUI, "ui", QtDebugMsg)

/// 主窗口日志
Q_LOGGING_CATEGORY(lcMainWindow, "ui.mainwindow", QtDebugMsg)

// ============================================================================
// 专用处理模块日志分类定义
// ============================================================================

/// 线程通信日志
Q_LOGGING_CATEGORY(lcThreading, "core.threading", QtDebugMsg)

// ============================================================================
// 测试模块日志分类定义
// ============================================================================

/// 测试主模块日志
Q_LOGGING_CATEGORY(lcTest, "test", QtDebugMsg)

// ============================================================================
// 新分类体系 — 六棵一级树（迁移过渡期，与旧定义共存）
// ============================================================================

// --- 核心模块 core.* ---
Q_LOGGING_CATEGORY(lcCoreProtocol, "core.protocol", QtDebugMsg)
Q_LOGGING_CATEGORY(lcCoreThreading, "core.threading", QtDebugMsg)
Q_LOGGING_CATEGORY(lcCoreConfig, "core.config", QtDebugMsg)

// --- 服务端 server.* ---
Q_LOGGING_CATEGORY(lcServerNetwork, "server.network", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerCapture, "server.capture", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerCaptureDxgi, "server.capture.dxgi", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerEncode, "server.encode", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerQueue, "server.queue", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerClientHandler, "server.clienthandler", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerInput, "server.input", QtDebugMsg)

// --- 客户端 client.* ---
Q_LOGGING_CATEGORY(lcClientSession, "client.session", QtDebugMsg)
Q_LOGGING_CATEGORY(lcClientSessionDecode, "client.session.decode", QtDebugMsg)
Q_LOGGING_CATEGORY(lcClientSessionProtocol, "client.session.protocol", QtDebugMsg)
Q_LOGGING_CATEGORY(lcClientGL, "client.gl", QtDebugMsg)

// --- UI ui.* ---
Q_LOGGING_CATEGORY(lcUIMainWindow, "ui.mainwindow", QtDebugMsg)
Q_LOGGING_CATEGORY(lcUIConnectionDialog, "ui.connectiondialog", QtDebugMsg)
Q_LOGGING_CATEGORY(lcUISettingsDialog, "ui.settingsdialog", QtDebugMsg)

// --- 测试 test.* ---
Q_LOGGING_CATEGORY(lcTestScreenCapture, "test.screencapture", QtDebugMsg)
Q_LOGGING_CATEGORY(lcTestScreenCaptureIntegration, "test.screencapture.integration", QtDebugMsg)
Q_LOGGING_CATEGORY(lcTestClientHandler, "test.clienthandler", QtDebugMsg)
Q_LOGGING_CATEGORY(lcTestProducerConsumer, "test.producerconsumer", QtDebugMsg)
