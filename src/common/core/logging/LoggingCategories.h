
#pragma once

#include <QtCore/QLoggingCategory>

// ============================================================================
// 核心模块日志分类
// ============================================================================

/// 应用程序主模块日志
Q_DECLARE_LOGGING_CATEGORY(lcApp)

/// 协议处理模块日志
Q_DECLARE_LOGGING_CATEGORY(lcProtocol)

// ============================================================================
// 服务端模块日志分类
// ============================================================================

/// 服务端主模块日志
Q_DECLARE_LOGGING_CATEGORY(lcServer)

/// 服务端管理器日志
Q_DECLARE_LOGGING_CATEGORY(lcServerManager)

/// 服务端网络模块日志
Q_DECLARE_LOGGING_CATEGORY(lcNetServer)

/// 输入模拟器日志
Q_DECLARE_LOGGING_CATEGORY(lcInputSimulator)

/// 客户端处理器Worker日志
Q_DECLARE_LOGGING_CATEGORY(lcClientHandlerWorker)

/// 屏幕捕获管理器日志
Q_DECLARE_LOGGING_CATEGORY(lcScreenCaptureManager)

/// 屏幕捕获Worker日志
Q_DECLARE_LOGGING_CATEGORY(lcScreenCaptureWorker)

/// DXGI Desktop Duplication 捕获引擎日志
Q_DECLARE_LOGGING_CATEGORY(lcDxgiCapture)

/// 队列管理器日志
Q_DECLARE_LOGGING_CATEGORY(lcQueueManager)

/// 数据处理Worker日志
Q_DECLARE_LOGGING_CATEGORY(lcDataProcessingWorker)

/// 数据处理模块日志
Q_DECLARE_LOGGING_CATEGORY(lcDataProcessing)

/// 数据处理配置日志
Q_DECLARE_LOGGING_CATEGORY(lcDataProcessingConfig)

/// 键盘模拟器日志(Linux)
Q_DECLARE_LOGGING_CATEGORY(lcKeyboardSimulatorLinux)

/// 键盘模拟器日志(macOS)
Q_DECLARE_LOGGING_CATEGORY(lcKeyboardSimulatorMacOS)

/// 键盘模拟器日志(Windows)
Q_DECLARE_LOGGING_CATEGORY(lcKeyboardSimulatorWindows)

/// 鼠标模拟器日志(Linux)
Q_DECLARE_LOGGING_CATEGORY(lcMouseSimulatorLinux)

/// 鼠标模拟器日志(macOS)
Q_DECLARE_LOGGING_CATEGORY(lcMouseSimulatorMacOS)

/// 鼠标模拟器日志(Windows)
Q_DECLARE_LOGGING_CATEGORY(lcMouseSimulatorWindows)

// ============================================================================
// 客户端模块日志分类
// ============================================================================

/// 客户端主模块日志
Q_DECLARE_LOGGING_CATEGORY(lcClient)

/// 客户端远程窗口日志
Q_DECLARE_LOGGING_CATEGORY(lcClientRemoteWindow)

/// 会话模块日志（新架构：DecodePipeline / ProtocolSession / RemoteDesktopSession）
Q_DECLARE_LOGGING_CATEGORY(lcSession)

/// 解码管线日志（帧解码）
Q_DECLARE_LOGGING_CATEGORY(lcDecodePipeline)

/// OpenGL纹理视口日志
Q_DECLARE_LOGGING_CATEGORY(lcGLViewport)

/// 客户端刷新性能度量日志
Q_DECLARE_LOGGING_CATEGORY(lcRefreshMetrics)

// ============================================================================
// 用户界面模块日志分类
// ============================================================================

/// UI主模块日志
Q_DECLARE_LOGGING_CATEGORY(lcUI)

/// 主窗口日志
Q_DECLARE_LOGGING_CATEGORY(lcMainWindow)

// ============================================================================
// 专用处理模块日志分类
// ============================================================================

/// 线程通信日志
Q_DECLARE_LOGGING_CATEGORY(lcThreading)

// ============================================================================
// 测试模块日志分类
// ============================================================================

/// 测试主模块日志
Q_DECLARE_LOGGING_CATEGORY(lcTest)

