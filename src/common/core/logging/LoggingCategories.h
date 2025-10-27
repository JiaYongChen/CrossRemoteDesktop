
#ifndef LOGGING_CATEGORIES_H
#define LOGGING_CATEGORIES_H

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

/**
 * @brief 日志分类管理类
 *
 * 提供统一的日志分类定义和管理功能，支持动态日志级别控制。
 * 按功能模块组织日志分类，便于调试和问题定位。
 */
class LoggingCategories : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 日志级别枚举
     */
    enum LogLevel {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Critical = 3,
        Fatal = 4
    };
    Q_ENUM(LogLevel)

        /**
         * @brief 获取单例实例
         */
        static LoggingCategories* instance();

    /**
     * @brief 设置全局日志级别
     * @param level 日志级别
     */
    static void setGlobalLogLevel(LogLevel level);

    /**
     * @brief 设置特定分类的日志级别
     * @param categoryName 分类名称
     * @param level 日志级别
     */
    static void setCategoryLogLevel(const QString& categoryName, LogLevel level);

    /**
     * @brief 获取所有日志分类名称
     * @return 分类名称列表
     */
    static QStringList getAllCategoryNames();

private:
    explicit LoggingCategories(QObject* parent = nullptr);
    static LoggingCategories* s_instance;
};

// ============================================================================
// 核心模块日志分类
// ============================================================================

/// 应用程序主模块日志
Q_DECLARE_LOGGING_CATEGORY(lcApp)

/// 协议处理模块日志
Q_DECLARE_LOGGING_CATEGORY(lcProtocol)

/// 加密模块日志
Q_DECLARE_LOGGING_CATEGORY(lcEncryption)

/// 性能监控日志
Q_DECLARE_LOGGING_CATEGORY(lcPerformance)

/// 内存管理日志
Q_DECLARE_LOGGING_CATEGORY(lcMemory)

/// 配置管理日志
Q_DECLARE_LOGGING_CATEGORY(lcConfig)

// ============================================================================
// 服务端模块日志分类
// ============================================================================

/// 服务端主模块日志
Q_DECLARE_LOGGING_CATEGORY(lcServer)

/// 服务端管理器日志
Q_DECLARE_LOGGING_CATEGORY(lcServerManager)

/// 屏幕捕获模块日志
Q_DECLARE_LOGGING_CATEGORY(lcCapture)

/// 服务端网络模块日志
Q_DECLARE_LOGGING_CATEGORY(lcNetServer)

/// 数据处理器日志
Q_DECLARE_LOGGING_CATEGORY(lcDataProcessor)

/// 输入模拟器日志
Q_DECLARE_LOGGING_CATEGORY(lcInputSimulator)

/// 客户端处理器日志
Q_DECLARE_LOGGING_CATEGORY(lcClientHandler)

// ============================================================================
// 客户端模块日志分类
// ============================================================================

/// 客户端主模块日志
Q_DECLARE_LOGGING_CATEGORY(lcClient)

/// 客户端窗口模块日志
Q_DECLARE_LOGGING_CATEGORY(lcClientWindow)

/// 客户端网络模块日志
Q_DECLARE_LOGGING_CATEGORY(lcNetClient)

/// 连接管理器日志
Q_DECLARE_LOGGING_CATEGORY(lcConnectionManager)

/// 客户端管理器日志（新增，用于ClientManager模块）
Q_DECLARE_LOGGING_CATEGORY(lcClientManager)

/// 会话管理器日志
Q_DECLARE_LOGGING_CATEGORY(lcSessionManager)

/// 渲染管理器日志
Q_DECLARE_LOGGING_CATEGORY(lcRenderManager)

/// 输入处理器日志
Q_DECLARE_LOGGING_CATEGORY(lcInputHandler)

// ============================================================================
// 用户界面模块日志分类
// ============================================================================

/// UI主模块日志
Q_DECLARE_LOGGING_CATEGORY(lcUI)

/// 主窗口日志
Q_DECLARE_LOGGING_CATEGORY(lcMainWindow)

/// 设置界面日志
Q_DECLARE_LOGGING_CATEGORY(lcSettings)

/// 状态栏日志
Q_DECLARE_LOGGING_CATEGORY(lcStatusBar)

// ============================================================================
// 专用处理模块日志分类
// ============================================================================

/// 线程通信日志
Q_DECLARE_LOGGING_CATEGORY(lcThreading)

/// SSL/TLS日志
Q_DECLARE_LOGGING_CATEGORY(lcSSL)

/// 自适应处理日志
Q_DECLARE_LOGGING_CATEGORY(lcAdaptive)

// ============================================================================
// 测试模块日志分类
// ============================================================================

/// 测试主模块日志
Q_DECLARE_LOGGING_CATEGORY(lcTest)

/// 单元测试日志
Q_DECLARE_LOGGING_CATEGORY(lcUnitTest)

/// 集成测试日志
Q_DECLARE_LOGGING_CATEGORY(lcIntegrationTest)

/// 性能测试日志
Q_DECLARE_LOGGING_CATEGORY(lcPerformanceTest)

// ============================================================================
// 便利宏定义
// ============================================================================

/// 应用程序调试日志宏
#define qCDebugApp(msg, ...) qCDebug(lcApp, msg, ##__VA_ARGS__)
#define qCInfoApp(msg, ...) qCInfo(lcApp, msg, ##__VA_ARGS__)
#define qCWarningApp(msg, ...) qCWarning(lcApp, msg, ##__VA_ARGS__)
#define qCCriticalApp(msg, ...) qCCritical(lcApp, msg, ##__VA_ARGS__)

/// 性能监控日志宏
#define qCDebugPerf(msg, ...) qCDebug(lcPerformance, msg, ##__VA_ARGS__)
#define qCInfoPerf(msg, ...) qCInfo(lcPerformance, msg, ##__VA_ARGS__)

/// 网络通信日志宏
#define qCDebugNet(msg, ...) qCDebug(lcNetServer, msg, ##__VA_ARGS__)
#define qCWarningNet(msg, ...) qCWarning(lcNetServer, msg, ##__VA_ARGS__)

#endif // LOGGING_CATEGORIES_H
