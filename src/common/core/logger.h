#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QThread>
#include <QQueue>
#include <QTimer>
#include <QDir>
#include <QStandardPaths>

class Logger : public QObject
{
    Q_OBJECT
    
public:
    // 日志级别
    enum LogLevel {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warning = 3,
        Error = 4,
        Critical = 5,
        Fatal = 6
    };
    Q_ENUM(LogLevel)
    
    // 日志输出目标
    enum LogTarget {
        Console = 0x01,
        File = 0x02,
        Network = 0x04,
        SystemLog = 0x08,
        All = Console | File | Network | SystemLog
    };
    Q_DECLARE_FLAGS(LogTargets, LogTarget)
    
    // 日志格式
    enum LogFormat {
        Simple,     // [LEVEL] Message
        Standard,   // [YYYY-MM-DD hh:mm:ss] [LEVEL] [Thread] Message
        Detailed,   // [YYYY-MM-DD hh:mm:ss.zzz] [LEVEL] [Thread:ID] [File:Line] Message
        Json,       // JSON格式
        Custom      // 自定义格式
    };
    
    // 日志轮转策略
    enum RotationPolicy {
        NoRotation,
        SizeBasedRotation,
        TimeBasedRotation,
        CountBasedRotation
    };
    
    // 单例模式
    static Logger* instance();
    static void destroyInstance();
    
    // 配置
    void setLogLevel(LogLevel level);
    LogLevel logLevel() const;
    
    void setLogTargets(LogTargets targets);
    LogTargets logTargets() const;
    
    void setLogFormat(LogFormat format);
    LogFormat logFormat() const;
    
    void setCustomFormat(const QString &format);
    QString customFormat() const;
    
    // 文件日志配置
    void setLogFile(const QString &filePath);
    QString logFile() const;
    
    void setMaxFileSize(qint64 maxSize); // 字节
    qint64 maxFileSize() const;
    
    void setMaxFileCount(int maxCount);
    int maxFileCount() const;
    
    void setRotationPolicy(RotationPolicy policy);
    RotationPolicy rotationPolicy() const;
    
    void setRotationInterval(int hours); // 小时
    int rotationInterval() const;
    
    // 缓冲配置
    void setBufferSize(int size);
    int bufferSize() const;
    
    void setFlushInterval(int milliseconds);
    int flushInterval() const;
    
    void setAutoFlush(bool enabled);
    bool autoFlush() const;
    
    // 网络日志配置
    void setNetworkEndpoint(const QString &host, quint16 port);
    QString networkHost() const;
    quint16 networkPort() const;
    
    // 过滤器
    void addFilter(const QString &pattern);
    void removeFilter(const QString &pattern);
    void clearFilters();
    QStringList filters() const;
    
    // 日志记录
    void log(LogLevel level, const QString &message, const QString &category = QString());
    void trace(const QString &message, const QString &category = QString());
    void debug(const QString &message, const QString &category = QString());
    void info(const QString &message, const QString &category = QString());
    void warning(const QString &message, const QString &category = QString());
    void error(const QString &message, const QString &category = QString());
    void critical(const QString &message, const QString &category = QString());
    void fatal(const QString &message, const QString &category = QString());
    
    // 格式化日志记录
    template<typename... Args>
    void logf(LogLevel level, const QString &format, Args&&... args) {
        log(level, QString::asprintf(format.toLocal8Bit().constData(), args...));
    }
    
    template<typename... Args>
    void tracef(const QString &format, Args&&... args) {
        logf(Trace, format, args...);
    }
    
    template<typename... Args>
    void debugf(const QString &format, Args&&... args) {
        logf(Debug, format, args...);
    }
    
    template<typename... Args>
    void infof(const QString &format, Args&&... args) {
        logf(Info, format, args...);
    }
    
    template<typename... Args>
    void warningf(const QString &format, Args&&... args) {
        logf(Warning, format, args...);
    }
    
    template<typename... Args>
    void errorf(const QString &format, Args&&... args) {
        logf(Error, format, args...);
    }
    
    template<typename... Args>
    void criticalf(const QString &format, Args&&... args) {
        logf(Critical, format, args...);
    }
    
    template<typename... Args>
    void fatalf(const QString &format, Args&&... args) {
        logf(Fatal, format, args...);
    }
    
    // 控制
    void flush();
    void rotate();
    void clear();
    
    // 状态
    bool isEnabled() const;
    void setEnabled(bool enabled);
    
    qint64 totalLogCount() const;
    qint64 totalLogSize() const;
    
    // 工具函数
    static QString levelToString(LogLevel level);
    static LogLevel stringToLevel(const QString &levelStr);
    static QString formatToString(LogFormat format);
    static LogFormat stringToFormat(const QString &formatStr);
    
    // Qt消息处理器集成
    static void installMessageHandler();
    static void uninstallMessageHandler();
    
signals:
    void logMessage(LogLevel level, const QString &message, const QString &category, const QDateTime &timestamp);
    void fileRotated(const QString &oldFile, const QString &newFile);
    void errorOccurred(const QString &error);
    
public slots:
    void onFlushTimer();
    void onRotationTimer();
    
protected:
    explicit Logger(QObject *parent = nullptr);
    ~Logger() override;
    
private slots:
    void processLogQueue();
    
private:
    struct LogEntry {
        LogLevel level;
        QString message;
        QString category;
        QDateTime timestamp;
        QString threadName;
        quintptr threadId;
        QString fileName;
        int lineNumber;
    };
    
    // 格式化
    QString formatMessage(const LogEntry &entry) const;
    QString formatSimple(const LogEntry &entry) const;
    QString formatStandard(const LogEntry &entry) const;
    QString formatDetailed(const LogEntry &entry) const;
    QString formatJson(const LogEntry &entry) const;
    QString formatCustom(const LogEntry &entry) const;
    
    // 输出
    void writeToConsole(const QString &formattedMessage, LogLevel level);
    void writeToFile(const QString &formattedMessage);
    void writeToNetwork(const QString &formattedMessage);
    void writeToSystemLog(const QString &formattedMessage, LogLevel level);
    
    // 文件管理
    void openLogFile();
    void closeLogFile();
    void rotateLogFile();
    bool shouldRotate() const;
    QString generateRotatedFileName() const;
    void cleanupOldLogFiles();
    
    // 过滤
    bool shouldLog(LogLevel level, const QString &message, const QString &category) const;
    
    // 网络
    void connectToNetworkEndpoint();
    void disconnectFromNetworkEndpoint();
    
    // Qt消息处理器
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    
    // 成员变量
    static Logger *s_instance;
    static QMutex s_instanceMutex;
    
    LogLevel m_logLevel;
    LogTargets m_logTargets;
    LogFormat m_logFormat;
    QString m_customFormat;
    
    // 文件相关
    QString m_logFilePath;
    QFile *m_logFile;
    QTextStream *m_logStream;
    qint64 m_maxFileSize;
    int m_maxFileCount;
    RotationPolicy m_rotationPolicy;
    int m_rotationInterval;
    QDateTime m_lastRotation;
    
    // 缓冲相关
    QQueue<LogEntry> m_logQueue;
    QMutex m_queueMutex;
    int m_bufferSize;
    QTimer *m_flushTimer;
    int m_flushInterval;
    bool m_autoFlush;
    
    // 网络相关
    QString m_networkHost;
    quint16 m_networkPort;
    QObject *m_networkSocket; // QTcpSocket或QUdpSocket
    
    // 过滤器
    QStringList m_filters;
    
    // 状态
    bool m_enabled;
    qint64 m_totalLogCount;
    qint64 m_totalLogSize;
    
    // 线程安全
    mutable QMutex m_mutex;
    
    // Qt消息处理器
    static QtMessageHandler s_previousHandler;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Logger::LogTargets)

// 便利宏
#define LOG_TRACE(msg) Logger::instance()->trace(msg)
#define LOG_DEBUG(msg) Logger::instance()->debug(msg)
#define LOG_INFO(msg) Logger::instance()->info(msg)
#define LOG_WARNING(msg) Logger::instance()->warning(msg)
#define LOG_ERROR(msg) Logger::instance()->error(msg)
#define LOG_CRITICAL(msg) Logger::instance()->critical(msg)
#define LOG_FATAL(msg) Logger::instance()->fatal(msg)

#define LOG_TRACEF(fmt, ...) Logger::instance()->tracef(fmt, __VA_ARGS__)
#define LOG_DEBUGF(fmt, ...) Logger::instance()->debugf(fmt, __VA_ARGS__)
#define LOG_INFOF(fmt, ...) Logger::instance()->infof(fmt, __VA_ARGS__)
#define LOG_WARNINGF(fmt, ...) Logger::instance()->warningf(fmt, __VA_ARGS__)
#define LOG_ERRORF(fmt, ...) Logger::instance()->errorf(fmt, __VA_ARGS__)
#define LOG_CRITICALF(fmt, ...) Logger::instance()->criticalf(fmt, __VA_ARGS__)
#define LOG_FATALF(fmt, ...) Logger::instance()->fatalf(fmt, __VA_ARGS__)

// 作用域日志类（用于函数进入/退出日志）
class ScopeLogger
{
public:
    explicit ScopeLogger(const QString &functionName, Logger::LogLevel level = Logger::Debug);
    ~ScopeLogger();
    
    void setExitMessage(const QString &message);
    
private:
    QString m_functionName;
    Logger::LogLevel m_level;
    QString m_exitMessage;
    QDateTime m_startTime;
};

#define LOG_SCOPE() ScopeLogger _scopeLogger(Q_FUNC_INFO)
#define LOG_SCOPE_LEVEL(level) ScopeLogger _scopeLogger(Q_FUNC_INFO, level)

// 性能日志类
class PerformanceLogger
{
public:
    explicit PerformanceLogger(const QString &operationName, Logger::LogLevel level = Logger::Info);
    ~PerformanceLogger();
    
    void checkpoint(const QString &checkpointName);
    void setThreshold(qint64 thresholdMs);
    
private:
    QString m_operationName;
    Logger::LogLevel m_level;
    QDateTime m_startTime;
    qint64 m_threshold;
    QList<QPair<QString, QDateTime>> m_checkpoints;
};

#define LOG_PERFORMANCE(name) PerformanceLogger _perfLogger(name)
#define LOG_PERFORMANCE_LEVEL(name, level) PerformanceLogger _perfLogger(name, level)

#endif // LOGGER_H