#ifndef LOGGER_H
#define LOGGER_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QMutex>

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
        All = Console | File
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
    
    // 单例模式
    static Logger* instance();
    
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
    
    // 日志记录
    void log(LogLevel level, const QString &message, const QString &category = QString());
    void trace(const QString &message, const QString &category = QString());
    void debug(const QString &message, const QString &category = QString());
    void info(const QString &message, const QString &category = QString());
    void warning(const QString &message, const QString &category = QString());
    void error(const QString &message, const QString &category = QString());
    void critical(const QString &message, const QString &category = QString());
    void fatal(const QString &message, const QString &category = QString());

    // 带源文件上下文的日志（内部使用，供 Qt 消息处理器调用）
    void logWithContext(LogLevel level,
                        const QString &message,
                        const QString &category,
                        const char *file,
                        int line,
                        const char *function);
    
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
    
    // Qt消息处理器集成
    static void installMessageHandler();
    static void uninstallMessageHandler();
    
    // Qt logging rules（QLoggingCategory::setFilterRules）
    // 允许从配置或环境变量动态设置分类日志开关
    static void applyQtLoggingRules(const QString &rules);
    
signals:
    void logMessage(LogLevel level, const QString &message, const QString &category, const QDateTime &timestamp);
    void fileRotated(const QString &oldFile, const QString &newFile);
    void errorOccurred(const QString &error);
    
public slots:
    
protected:
    explicit Logger(QObject *parent = nullptr);
    ~Logger() override;
    
private slots:
    
private:
    struct LogEntry {
        LogLevel level;
        QString message;
        QString category;
        QDateTime timestamp;
        quintptr threadId;
        QString fileName;
        int lineNumber;
    QString functionName;
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
    
    // 文件管理
    void openLogFile();
    void closeLogFile();
    void rotateLogFile();
    bool shouldRotate() const;
    QString generateRotatedFileName() const;
    void cleanupOldLogFiles();
    
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
    
    // 状态
    bool m_enabled;
    qint64 m_totalLogCount;
    qint64 m_totalLogSize;
    
    // 线程安全
    mutable QMutex m_mutex;
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

#endif // LOGGER_H