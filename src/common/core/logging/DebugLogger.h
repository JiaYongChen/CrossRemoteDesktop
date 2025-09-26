#ifndef DEBUG_LOGGER_H
#define DEBUG_LOGGER_H

#include <QtCore/QObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMutex>
#include <QtCore/QTextStream>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <memory>

/**
 * @brief 增强的调试日志管理器
 * 
 * 提供详细的日志输出功能，包括：
 * - 多级别日志控制
 * - 文件和控制台输出
 * - 性能监控日志
 * - 内存使用日志
 * - 网络通信日志
 * - 线程安全的日志记录
 */
class DebugLogger : public QObject
{
    Q_OBJECT
    
public:
    /**
     * @brief 日志级别枚举
     */
    enum LogLevel {
        Trace = 0,      ///< 最详细的跟踪信息
        Debug = 1,      ///< 调试信息
        Info = 2,       ///< 一般信息
        Warning = 3,    ///< 警告信息
        Error = 4,      ///< 错误信息
        Critical = 5,   ///< 严重错误
        Fatal = 6       ///< 致命错误
    };
    Q_ENUM(LogLevel)
    
    /**
     * @brief 日志类型枚举
     */
    enum LogType {
        General = 0,        ///< 一般日志
        Performance = 1,    ///< 性能日志
        Memory = 2,         ///< 内存日志
        Network = 3,        ///< 网络日志
        Threading = 4,      ///< 线程日志
        Security = 5        ///< 安全日志
    };
    Q_ENUM(LogType)
    
    /**
     * @brief 获取单例实例
     */
    static DebugLogger* instance();
    
    /**
     * @brief 初始化日志系统
     * @param configFile 配置文件路径
     * @return 是否初始化成功
     */
    bool initialize(const QString& configFile = QString());
    
    /**
     * @brief 设置日志级别
     * @param level 日志级别
     */
    void setLogLevel(LogLevel level);
    
    /**
     * @brief 设置特定类型的日志级别
     * @param type 日志类型
     * @param level 日志级别
     */
    void setLogLevel(LogType type, LogLevel level);
    
    /**
     * @brief 启用文件日志
     * @param filePath 日志文件路径
     * @param maxSize 最大文件大小（字节）
     * @param backupCount 备份文件数量
     */
    void enableFileLogging(const QString& filePath, qint64 maxSize = 10 * 1024 * 1024, int backupCount = 5);
    
    /**
     * @brief 启用控制台日志
     * @param enabled 是否启用
     */
    void enableConsoleLogging(bool enabled = true);
    
    /**
     * @brief 记录一般日志
     */
    void log(LogLevel level, const QString& category, const QString& message, 
             const QString& function = QString(), int line = -1);
    
    /**
     * @brief 记录性能日志
     */
    void logPerformance(const QString& operation, qint64 duration, 
                       const QHash<QString, QVariant>& metrics = QHash<QString, QVariant>());
    
    /**
     * @brief 记录内存使用日志
     */
    void logMemoryUsage(const QString& context, qint64 memoryUsed, qint64 memoryTotal = -1);
    
    /**
     * @brief 记录网络通信日志
     */
    void logNetworkActivity(const QString& operation, const QString& endpoint, 
                           qint64 bytesTransferred, qint64 duration);
    
    /**
     * @brief 记录线程活动日志
     */
    void logThreadActivity(const QString& threadName, const QString& activity, 
                          const QHash<QString, QVariant>& details = QHash<QString, QVariant>());
    
    /**
     * @brief 开始性能计时
     * @param operationId 操作标识
     * @return 计时器ID
     */
    QString startPerformanceTimer(const QString& operationId);
    
    /**
     * @brief 结束性能计时并记录
     * @param timerId 计时器ID
     * @param additionalInfo 附加信息
     */
    void endPerformanceTimer(const QString& timerId, const QString& additionalInfo = QString());
    
    /**
     * @brief 获取当前日志统计信息
     */
    QHash<QString, QVariant> getLogStatistics() const;
    
    /**
     * @brief 刷新日志缓冲区
     */
    void flush();
    
public slots:
    /**
     * @brief 处理Qt消息
     */
    void handleQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message);
    
    /**
     * @brief 定期刷新日志
     */
    void periodicFlush();
    
    /**
     * @brief 检查并轮转日志文件
     */
    void checkLogRotation();
    
signals:
    /**
     * @brief 日志消息信号
     */
    void logMessage(LogLevel level, LogType type, const QString& message);
    
    /**
     * @brief 性能指标信号
     */
    void performanceMetric(const QString& operation, qint64 duration, const QHash<QString, QVariant>& metrics);
    
private:
    explicit DebugLogger(QObject* parent = nullptr);
    ~DebugLogger();
    
    /**
     * @brief 格式化日志消息
     */
    QString formatMessage(LogLevel level, LogType type, const QString& category, 
                         const QString& message, const QString& function, int line) const;
    
    /**
     * @brief 写入日志到文件
     */
    void writeToFile(const QString& message);
    
    /**
     * @brief 写入日志到控制台
     */
    void writeToConsole(LogLevel level, const QString& message);
    
    /**
     * @brief 轮转日志文件
     */
    void rotateLogFile();
    
    /**
     * @brief 加载配置文件
     */
    bool loadConfiguration(const QString& configFile);
    
    /**
     * @brief 获取日志级别字符串
     */
    QString logLevelToString(LogLevel level) const;
    
    /**
     * @brief 获取日志类型字符串
     */
    QString logTypeToString(LogType type) const;
    
private:
    static DebugLogger* s_instance;     ///< 单例实例
    static QMutex s_mutex;              ///< 单例互斥锁
    
    mutable QMutex m_mutex;             ///< 日志记录互斥锁
    
    // 日志级别设置
    LogLevel m_globalLogLevel;          ///< 全局日志级别
    QHash<LogType, LogLevel> m_typeLevels;  ///< 各类型日志级别
    
    // 文件日志设置
    bool m_fileLoggingEnabled;          ///< 是否启用文件日志
    QString m_logFilePath;              ///< 日志文件路径
    qint64 m_maxFileSize;               ///< 最大文件大小
    int m_backupCount;                  ///< 备份文件数量
    std::unique_ptr<QFile> m_logFile;   ///< 日志文件对象
    std::unique_ptr<QTextStream> m_logStream;  ///< 日志文件流
    
    // 控制台日志设置
    bool m_consoleLoggingEnabled;       ///< 是否启用控制台日志
    
    // 性能计时器
    QHash<QString, QDateTime> m_performanceTimers;  ///< 性能计时器
    
    // 日志统计
    mutable QMutex m_statsMutex;        ///< 统计信息互斥锁
    QHash<LogLevel, qint64> m_logCounts;    ///< 各级别日志计数
    QHash<LogType, qint64> m_typeCounts;    ///< 各类型日志计数
    qint64 m_totalLogCount;             ///< 总日志计数
    QDateTime m_startTime;              ///< 开始时间
    
    // 定时器
    QTimer* m_flushTimer;               ///< 刷新定时器
    QTimer* m_rotationTimer;            ///< 轮转检查定时器
};

// 便利宏定义
#define DEBUG_LOG(level, category, message) \
    DebugLogger::instance()->log(DebugLogger::level, category, message, Q_FUNC_INFO, __LINE__)

#define DEBUG_TRACE(category, message) DEBUG_LOG(Trace, category, message)
#define DEBUG_DEBUG(category, message) DEBUG_LOG(Debug, category, message)
#define DEBUG_INFO(category, message) DEBUG_LOG(Info, category, message)
#define DEBUG_WARNING(category, message) DEBUG_LOG(Warning, category, message)
#define DEBUG_ERROR(category, message) DEBUG_LOG(Error, category, message)
#define DEBUG_CRITICAL(category, message) DEBUG_LOG(Critical, category, message)
#define DEBUG_FATAL(category, message) DEBUG_LOG(Fatal, category, message)

#define DEBUG_PERF_START(operation) \
    DebugLogger::instance()->startPerformanceTimer(operation)

#define DEBUG_PERF_END(timerId, info) \
    DebugLogger::instance()->endPerformanceTimer(timerId, info)

#define DEBUG_MEMORY(context, used, total) \
    DebugLogger::instance()->logMemoryUsage(context, used, total)

#define DEBUG_NETWORK(operation, endpoint, bytes, duration) \
    DebugLogger::instance()->logNetworkActivity(operation, endpoint, bytes, duration)

#define DEBUG_THREAD(name, activity, details) \
    DebugLogger::instance()->logThreadActivity(name, activity, details)

#endif // DEBUG_LOGGER_H