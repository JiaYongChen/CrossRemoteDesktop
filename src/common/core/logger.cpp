#include "logger.h"
#include "constants.h"
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QTextStream>
#include <QMutexLocker>
#include <QThread>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <iostream>

// 静态成员变量定义
Logger* Logger::s_instance = nullptr;
QMutex Logger::s_instanceMutex;

Logger::Logger(QObject *parent)
    : QObject(parent)
    , m_logLevel(LogLevel::Info)
    , m_logTargets(LogTarget::Console | LogTarget::File)
    , m_logFormat(LogFormat::Detailed)
    , m_logFile(nullptr)
    , m_logStream(nullptr)
    , m_maxFileSize(CoreConstants::DEFAULT_MAX_FILE_SIZE)
    , m_maxFileCount(5)
    , m_rotationPolicy(NoRotation)
    , m_rotationInterval(24)
    , m_bufferSize(1000)
    , m_flushTimer(new QTimer(this))
    , m_flushInterval(5000) // 5秒
    , m_autoFlush(false)
    , m_networkPort(0)
    , m_networkSocket(nullptr)
    , m_enabled(true)
    , m_totalLogCount(0)
    , m_totalLogSize(0)
{
    // 设置默认日志文件路径
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString logDir = appDataPath + "/logs";
    
    // 创建日志目录
    QDir().mkpath(logDir);
    
    // 设置默认日志文件路径
    m_logFilePath = logDir + "/application.log";
    
    // 初始化自动刷新定时器
    m_flushTimer->setSingleShot(false);
    connect(m_flushTimer, &QTimer::timeout, this, &Logger::flush);
    m_flushTimer->start(m_flushInterval);
    
    // 安装Qt消息处理器
    qInstallMessageHandler(Logger::qtMessageHandler);
}

Logger::~Logger()
{
    flush();
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
    qInstallMessageHandler(nullptr);
}

Logger* Logger::instance()
{
    if (s_instance == nullptr) {
        QMutexLocker locker(&s_instanceMutex);
        if (s_instance == nullptr) {
            s_instance = new Logger();
        }
    }
    return s_instance;
}

void Logger::setLogLevel(LogLevel level)
{
    QMutexLocker locker(&m_mutex);
    m_logLevel = level;
}

Logger::LogLevel Logger::logLevel() const
{
    QMutexLocker locker(&m_mutex);
    return m_logLevel;
}

void Logger::setLogTargets(LogTargets targets)
{
    QMutexLocker locker(&m_mutex);
    m_logTargets = targets;
    
    if (targets & LogTarget::File) {
        openLogFile();
    }
}

Logger::LogTargets Logger::logTargets() const
{
    QMutexLocker locker(&m_mutex);
    return m_logTargets;
}

void Logger::setLogFormat(LogFormat format)
{
    QMutexLocker locker(&m_mutex);
    m_logFormat = format;
}

Logger::LogFormat Logger::logFormat() const
{
    QMutexLocker locker(&m_mutex);
    return m_logFormat;
}

void Logger::setLogFile(const QString &filePath)
{
    QMutexLocker locker(&m_mutex);
    m_logFilePath = filePath;
    
    if (m_logTargets & LogTarget::File) {
        openLogFile();
    }
}

QString Logger::logFile() const
{
    QMutexLocker locker(&m_mutex);
    return m_logFilePath;
}

void Logger::setMaxFileSize(qint64 maxSize)
{
    QMutexLocker locker(&m_mutex);
    m_maxFileSize = maxSize;
}

qint64 Logger::maxFileSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxFileSize;
}

void Logger::setMaxFileCount(int maxCount)
{
    QMutexLocker locker(&m_mutex);
    m_maxFileCount = maxCount;
}

int Logger::maxFileCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxFileCount;
}

void Logger::log(LogLevel level, const QString &message, const QString &category)
{
    if (!m_enabled || level < m_logLevel) {
        return;
    }
    
    // 应用过滤器
    if (!m_filters.isEmpty()) {
        bool passed = false;
        for (const auto &filter : m_filters) {
            if (message.contains(filter) || category.contains(filter)) {
                passed = true;
                break;
            }
        }
        if (!passed) {
            return; // 过滤器不匹配，排除此日志
        }
    }
    
    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.level = level;
    entry.message = message;
    entry.category = category;
    entry.fileName = QString();
    entry.lineNumber = 0;
    entry.threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    
    QMutexLocker locker(&m_mutex);
    
    // 更新统计
    m_totalLogCount++;
    // 写入日志
    QString formattedMessage = formatMessage(entry);
    
    // 输出到控制台
    if (m_logTargets & LogTarget::Console) {
        writeToConsole(formattedMessage, entry.level);
    }
    
    // 输出到文件
    if (m_logTargets & LogTarget::File) {
        writeToFile(formattedMessage);
    }
    
    // 输出到网络
    if (m_logTargets & LogTarget::Network) {
        writeToNetwork(formattedMessage);
    }
    
    // 输出到系统日志
    if (m_logTargets & LogTarget::SystemLog) {
        writeToSystemLog(formattedMessage, entry.level);
    }
}

void Logger::flush()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_logFile && m_logFile->isOpen()) {
        m_logStream->flush();
    }
}

void Logger::rotate()
{
    QMutexLocker locker(&m_mutex);
    rotateLogFile();
}

void Logger::clear()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
    
    if (!m_logFilePath.isEmpty()) {
        QFile::remove(m_logFilePath);
    }
    
    if (m_logTargets & LogTarget::File) {
        openLogFile();
    }
    
    // 重置统计
    m_totalLogCount = 0;
}

void Logger::addFilter(const QString &pattern)
{
    QMutexLocker locker(&m_mutex);
    m_filters.append(pattern);
}

void Logger::removeFilter(const QString &pattern)
{
    QMutexLocker locker(&m_mutex);
    m_filters.removeAll(pattern);
}

void Logger::clearFilters()
{
    QMutexLocker locker(&m_mutex);
    m_filters.clear();
}

QStringList Logger::filters() const
{
    QMutexLocker locker(&m_mutex);
    return m_filters;
}

bool Logger::isEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_enabled;
}

void Logger::setEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_enabled = enabled;
}

qint64 Logger::totalLogCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_totalLogCount;
}



qint64 Logger::totalLogSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_totalLogSize;
}

void Logger::openLogFile()
{
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
        delete m_logFile;
        delete m_logStream;
    }
    
    if (m_logFilePath.isEmpty()) {
        m_logFilePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs/app.log";
    }
    
    QDir().mkpath(QFileInfo(m_logFilePath).absolutePath());
    
    m_logFile = new QFile(m_logFilePath);
    
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        qWarning() << "Failed to open log file:" << m_logFilePath;
        return;
    }
    
    m_logStream = new QTextStream(m_logFile);
}

void Logger::writeToConsole(const QString &formattedMessage, LogLevel level)
{
    Q_UNUSED(level)
    std::cout << formattedMessage.toStdString() << std::endl;
}

void Logger::writeToFile(const QString &formattedMessage)
{
    if (m_logFile && m_logFile->isOpen()) {
        *m_logStream << formattedMessage << Qt::endl;
        
        // 检查文件大小并轮转
        if (m_logFile->size() > m_maxFileSize) {
            rotateLogFile();
        }
    }
}



QString Logger::formatMessage(const LogEntry &entry) const
{
    switch (m_logFormat) {
    case LogFormat::Simple:
        return formatSimple(entry);
    case LogFormat::Standard:
        return formatStandard(entry);
    case LogFormat::Detailed:
        return formatDetailed(entry);
    case LogFormat::Json:
        return formatJson(entry);
    case LogFormat::Custom:
        return formatCustom(entry);
    }
    return formatStandard(entry);
}

QString Logger::formatSimple(const LogEntry &entry) const
{
    return QString("[%1] %2")
           .arg(levelToString(entry.level))
           .arg(entry.message);
}

QString Logger::formatStandard(const LogEntry &entry) const
{
    return QString("[%1] [%2] [%3] %4")
           .arg(entry.timestamp.toString("yyyy-MM-dd hh:mm:ss"))
           .arg(levelToString(entry.level))
           .arg(QString::number(entry.threadId))
           .arg(entry.message);
}

QString Logger::formatDetailed(const LogEntry &entry) const
{
    return QString("[%1] [%2] [%3:%4] [%5:%6] %7")
           .arg(entry.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz"))
           .arg(levelToString(entry.level))
           .arg(QString::number(entry.threadId))
           .arg(entry.threadId)
           .arg(entry.fileName)
           .arg(entry.lineNumber)
           .arg(entry.message);
}

QString Logger::formatJson(const LogEntry &entry) const
{
    QJsonObject json;
    json["timestamp"] = entry.timestamp.toString(Qt::ISODate);
    json["level"] = levelToString(entry.level);
    json["message"] = entry.message;
    json["category"] = entry.category;
    json["thread"] = QString::number(entry.threadId);
    json["threadId"] = QString::number(entry.threadId);
    json["file"] = entry.fileName;
    json["line"] = entry.lineNumber;
    
    QJsonDocument doc(json);
    return doc.toJson(QJsonDocument::Compact);
}

QString Logger::formatCustom(const LogEntry &entry) const
{
    QString format = m_customFormat;
    if (format.isEmpty()) {
        return formatStandard(entry);
    }
    
    format.replace("%timestamp%", entry.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz"));
    format.replace("%level%", levelToString(entry.level));
    format.replace("%message%", entry.message);
    format.replace("%category%", entry.category);
    format.replace("%threadId%", QString::number(entry.threadId));
    format.replace("%file%", entry.fileName);
    format.replace("%line%", QString::number(entry.lineNumber));
    
    return format;
}

QString Logger::levelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARNING";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Critical: return "CRITICAL";
    case LogLevel::Fatal: return "FATAL";
    }
    return "UNKNOWN";
}

void Logger::writeToNetwork(const QString &formattedMessage)
{
    // 网络日志功能暂未实现
    Q_UNUSED(formattedMessage)
}

void Logger::writeToSystemLog(const QString &formattedMessage, LogLevel level)
{
    // 系统日志功能暂未实现
    Q_UNUSED(formattedMessage)
    Q_UNUSED(level)
}

void Logger::rotateLogFile()
{
    if (!m_logFile || !m_logFile->isOpen()) {
        return;
    }
    
    m_logFile->close();
    
    QFileInfo fileInfo(m_logFilePath);
    QString basePath = fileInfo.absolutePath() + "/" + fileInfo.baseName();
    QString extension = fileInfo.suffix();
    
    // 删除最旧的备份文件
    QString oldestBackup = QString("%1.%2.%3")
                          .arg(basePath)
                          .arg(m_maxFileCount)
                          .arg(extension);
    QFile::remove(oldestBackup);
    
    // 重命名现有备份文件
    for (int i = m_maxFileCount - 1; i >= 1; --i) {
        QString oldName = QString("%1.%2.%3")
                         .arg(basePath)
                         .arg(i)
                         .arg(extension);
        QString newName = QString("%1.%2.%3")
                         .arg(basePath)
                         .arg(i + 1)
                         .arg(extension);
        QFile::rename(oldName, newName);
    }
    
    // 重命名当前日志文件
    QString backupFile = QString("%1.1.%2")
                        .arg(basePath)
                        .arg(extension);
    QFile::rename(m_logFilePath, backupFile);
    
    // 重新打开日志文件
    openLogFile();
}



void Logger::qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    LogLevel level;
    switch (type) {
    case QtDebugMsg: level = LogLevel::Debug; break;
    case QtInfoMsg: level = LogLevel::Info; break;
    case QtWarningMsg: level = LogLevel::Warning; break;
    case QtCriticalMsg: level = LogLevel::Error; break;
    case QtFatalMsg: level = LogLevel::Fatal; break;
    default: level = LogLevel::Info; break;
    }
    
    Logger::instance()->log(level, msg, context.category);
}

// ScopeLogger 实现
ScopeLogger::ScopeLogger(const QString &functionName, Logger::LogLevel level)
    : m_functionName(functionName), m_level(level), m_startTime(QDateTime::currentDateTime())
{
    Logger::instance()->log(m_level, QString("Entering %1").arg(m_functionName), QString());
}

ScopeLogger::~ScopeLogger()
{
    Logger::instance()->log(m_level, QString("Exiting %1").arg(m_functionName), QString());
}

void ScopeLogger::setExitMessage(const QString &message)
{
    m_exitMessage = message;
}

// PerformanceLogger 实现
PerformanceLogger::PerformanceLogger(const QString &operationName, Logger::LogLevel level)
    : m_operationName(operationName), m_level(level), m_startTime(QDateTime::currentDateTime()), m_threshold(0)
{
    Logger::instance()->log(m_level, QString("Starting %1").arg(m_operationName), QString());
}

PerformanceLogger::~PerformanceLogger()
{
    qint64 elapsed = m_startTime.msecsTo(QDateTime::currentDateTime());
    Logger::instance()->log(m_level, QString("Completed %1 in %2ms").arg(m_operationName).arg(elapsed), QString());
}

void PerformanceLogger::checkpoint(const QString &checkpointName)
{
    m_checkpoints.append(qMakePair(checkpointName, QDateTime::currentDateTime()));
}

void PerformanceLogger::setThreshold(qint64 thresholdMs)
{
    m_threshold = thresholdMs;
}

// Logger missing function implementations
void Logger::onFlushTimer()
{
    flush();
}

void Logger::onRotationTimer()
{
    if (shouldRotate()) {
        rotate();
    }
}

void Logger::processLogQueue()
{
    QMutexLocker locker(&m_queueMutex);
    while (!m_logQueue.isEmpty()) {
        LogEntry entry = m_logQueue.dequeue();
        QString formattedMessage = formatMessage(entry);
        
        if (m_logTargets & LogTarget::Console) {
            writeToConsole(formattedMessage, entry.level);
        }
        if (m_logTargets & LogTarget::File) {
            writeToFile(formattedMessage);
        }
        if (m_logTargets & LogTarget::Network) {
            writeToNetwork(formattedMessage);
        }
        if (m_logTargets & LogTarget::SystemLog) {
            writeToSystemLog(formattedMessage, entry.level);
        }
    }
}

void Logger::setRotationPolicy(RotationPolicy policy)
{
    QMutexLocker locker(&m_mutex);
    m_rotationPolicy = policy;
}

Logger::RotationPolicy Logger::rotationPolicy() const
{
    QMutexLocker locker(&m_mutex);
    return m_rotationPolicy;
}

void Logger::setRotationInterval(int hours)
{
    QMutexLocker locker(&m_mutex);
    m_rotationInterval = hours;
}

int Logger::rotationInterval() const
{
    QMutexLocker locker(&m_mutex);
    return m_rotationInterval;
}

void Logger::trace(const QString &message, const QString &category)
{
    log(Trace, message, category);
}

void Logger::debug(const QString &message, const QString &category)
{
    log(Debug, message, category);
}

void Logger::info(const QString &message, const QString &category)
{
    log(Info, message, category);
}

void Logger::warning(const QString &message, const QString &category)
{
    log(Warning, message, category);
}

void Logger::error(const QString &message, const QString &category)
{
    log(Error, message, category);
}

void Logger::critical(const QString &message, const QString &category)
{
    log(Critical, message, category);
}

void Logger::fatal(const QString &message, const QString &category)
{
    log(Fatal, message, category);
}

bool Logger::shouldRotate() const
{
    if (m_rotationPolicy == NoRotation) {
        return false;
    }
    
    if (m_rotationPolicy == SizeBasedRotation) {
        if (m_logFile && m_logFile->size() >= m_maxFileSize) {
            return true;
        }
    }
    
    if (m_rotationPolicy == TimeBasedRotation) {
        QDateTime now = QDateTime::currentDateTime();
        if (m_lastRotation.isValid() && 
            m_lastRotation.secsTo(now) >= m_rotationInterval * 3600) {
            return true;
        }
    }
    
    return false;
}

void Logger::installMessageHandler()
{
    qInstallMessageHandler(Logger::qtMessageHandler);
}

void Logger::uninstallMessageHandler()
{
    qInstallMessageHandler(nullptr);
}