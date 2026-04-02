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

/// 数据处理器日志
Q_LOGGING_CATEGORY(lcDataProcessor, "server.dataprocessor", QtDebugMsg)

/// 输入模拟器日志
Q_LOGGING_CATEGORY(lcInputSimulator, "server.inputsimulator", QtDebugMsg)

/// 客户端处理器Worker日志
Q_LOGGING_CATEGORY(clientHandlerWorker, "clienthandler.worker", QtDebugMsg)

/// 屏幕捕获管理器日志
Q_LOGGING_CATEGORY(screenCaptureManager, "screencapture.manager", QtDebugMsg)

/// 屏幕捕获Worker日志
Q_LOGGING_CATEGORY(screenCaptureWorker, "screencapture.worker", QtDebugMsg)

/// 数据流日志
Q_LOGGING_CATEGORY(lcDataFlow, "dataflow", QtDebugMsg)

/// 队列管理器日志
Q_LOGGING_CATEGORY(lcQueueManager, "queuemanager", QtDebugMsg)

/// 数据处理日志
Q_LOGGING_CATEGORY(DataProcessingLog, "server.dataprocessing", QtDebugMsg)

/// 数据处理配置日志
Q_LOGGING_CATEGORY(lcDataProcessingConfig, "server.dataprocessing.config", QtDebugMsg)

/// 数据处理Worker日志
Q_LOGGING_CATEGORY(lcDataProcessingWorker, "dataprocessingworker", QtDebugMsg)

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

/// 客户端窗口模块日志
Q_LOGGING_CATEGORY(lcClientWindow, "client.window", QtDebugMsg)

/// 客户端远程窗口日志
Q_LOGGING_CATEGORY(lcClientRemoteWindow, "client.remote.window", QtDebugMsg)

/// 客户端管理器日志
Q_LOGGING_CATEGORY(lcClientManager, "client.manager", QtDebugMsg)

/// 会话管理器日志
Q_LOGGING_CATEGORY(lcSessionManager, "client.session", QtDebugMsg)

/// 渲染管理器日志
Q_LOGGING_CATEGORY(lcRenderManager, "client.render", QtDebugMsg)

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

/// 单元测试日志
Q_LOGGING_CATEGORY(lcUnitTest, "test.unit", QtDebugMsg)

/// 集成测试日志
Q_LOGGING_CATEGORY(lcIntegrationTest, "test.integration", QtDebugMsg)

/// 性能测试日志
Q_LOGGING_CATEGORY(lcPerformanceTest, "test.performance", QtDebugMsg)
