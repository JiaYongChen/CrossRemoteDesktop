#include "LoggingCategories.h"
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QHash>

// 静态成员定义
LoggingCategories* LoggingCategories::s_instance = nullptr;

// ============================================================================
// 核心模块日志分类定义
// ============================================================================

/// 应用程序主模块日志
Q_LOGGING_CATEGORY(lcApp, "app", QtDebugMsg)

/// 协议处理模块日志
Q_LOGGING_CATEGORY(lcProtocol, "core.protocol", QtDebugMsg)

/// 加密模块日志
Q_LOGGING_CATEGORY(lcEncryption, "core.encryption", QtDebugMsg)

/// 性能监控日志
Q_LOGGING_CATEGORY(lcPerformance, "performance", QtDebugMsg)

/// 内存管理日志
Q_LOGGING_CATEGORY(lcMemory, "core.memory", QtDebugMsg)

/// 配置管理日志
Q_LOGGING_CATEGORY(lcConfig, "core.config", QtDebugMsg)

// ============================================================================
// 服务端模块日志分类定义
// ============================================================================

/// 服务端主模块日志
Q_LOGGING_CATEGORY(lcServer, "server", QtDebugMsg)

/// 服务端管理器日志
Q_LOGGING_CATEGORY(lcServerManager, "server.manager", QtDebugMsg)

/// 屏幕捕获模块日志
Q_LOGGING_CATEGORY(lcCapture, "server.capture", QtDebugMsg)

/// 服务端网络模块日志
Q_LOGGING_CATEGORY(lcNetServer, "server.net", QtDebugMsg)

/// 数据处理器日志
Q_LOGGING_CATEGORY(lcDataProcessor, "server.dataprocessor", QtDebugMsg)

/// 输入模拟器日志
Q_LOGGING_CATEGORY(lcInputSimulator, "server.inputsimulator", QtDebugMsg)

/// 客户端处理器日志
Q_LOGGING_CATEGORY(lcClientHandler, "server.clienthandler", QtDebugMsg)

// ============================================================================
// 客户端模块日志分类定义
// ============================================================================

/// 客户端主模块日志
Q_LOGGING_CATEGORY(lcClient, "client", QtDebugMsg)

/// 客户端窗口模块日志
Q_LOGGING_CATEGORY(lcClientWindow, "client.window", QtDebugMsg)

/// 客户端网络模块日志
Q_LOGGING_CATEGORY(lcNetClient, "client.net", QtDebugMsg)

/// 连接管理器日志
Q_LOGGING_CATEGORY(lcConnectionManager, "client.connection", QtDebugMsg)

/// 客户端管理器日志（新增，用于ClientManager模块）
Q_LOGGING_CATEGORY(lcClientManager, "client.manager", QtDebugMsg)

/// 会话管理器日志
Q_LOGGING_CATEGORY(lcSessionManager, "client.session", QtDebugMsg)

/// 渲染管理器日志
Q_LOGGING_CATEGORY(lcRenderManager, "client.render", QtDebugMsg)

/// 输入处理器日志
Q_LOGGING_CATEGORY(lcInputHandler, "client.input", QtDebugMsg)

// ============================================================================
// 用户界面模块日志分类定义
// ============================================================================

/// UI主模块日志
Q_LOGGING_CATEGORY(lcUI, "ui", QtDebugMsg)

/// 主窗口日志
Q_LOGGING_CATEGORY(lcMainWindow, "ui.mainwindow", QtDebugMsg)

/// 设置界面日志
Q_LOGGING_CATEGORY(lcSettings, "ui.settings", QtDebugMsg)

/// 状态栏日志
Q_LOGGING_CATEGORY(lcStatusBar, "ui.statusbar", QtDebugMsg)

// ============================================================================
// 专用处理模块日志分类定义
// ============================================================================

/// 线程通信日志
Q_LOGGING_CATEGORY(lcThreading, "core.threading", QtDebugMsg)

/// SSL/TLS日志
Q_LOGGING_CATEGORY(lcSSL, "core.ssl", QtDebugMsg)

/// 自适应处理日志
Q_LOGGING_CATEGORY(lcAdaptive, "core.adaptive", QtDebugMsg)

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

// ============================================================================
// LoggingCategories类实现
// ============================================================================

LoggingCategories::LoggingCategories(QObject* parent)
    : QObject(parent) {
}

LoggingCategories* LoggingCategories::instance() {
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    if ( !s_instance ) {
        s_instance = new LoggingCategories();
    }

    return s_instance;
}

void LoggingCategories::setGlobalLogLevel(LogLevel level) {
    QtMsgType qtLevel = static_cast<QtMsgType>(level);

    // 设置所有分类的日志级别
    QLoggingCategory::setFilterRules(QString("*=%1").arg(qtLevel));
}

void LoggingCategories::setCategoryLogLevel(const QString& categoryName, LogLevel level) {
    QtMsgType qtLevel = static_cast<QtMsgType>(level);

    // 设置特定分类的日志级别
    QString rule = QString("%1=%2").arg(categoryName).arg(qtLevel);
    QLoggingCategory::setFilterRules(rule);
}

QStringList LoggingCategories::getAllCategoryNames() {
    static QStringList categories = {
        // 核心模块
        "app", "core.protocol", "core.encryption",
        "performance", "core.memory", "core.config",

        // 服务端模块
        "server", "server.manager", "server.capture", "server.net",
        "server.dataprocessor", "server.inputsimulator", "server.clienthandler",

        // 客户端模块
        "client", "client.window", "client.net", "client.connection",
        "client.manager",
        "client.session", "client.render", "client.input",

        // UI模块
        "ui", "ui.mainwindow", "ui.settings", "ui.statusbar",

        // 专用处理模块
        "core.threading", "core.ssl", "core.adaptive",

        // 测试模块
        "test", "test.unit", "test.integration", "test.performance"
    };

    return categories;
}
