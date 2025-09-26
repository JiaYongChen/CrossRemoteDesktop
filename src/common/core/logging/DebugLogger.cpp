#include "DebugLogger.h"
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QUuid>
#include <QtCore/QStringConverter>
#include <QtGui/QGuiApplication>
#include <iostream>

// 静态成员初始化
DebugLogger* DebugLogger::s_instance = nullptr;
QMutex DebugLogger::s_mutex;

DebugLogger::DebugLogger(QObject* parent)
    : QObject(parent)
    , m_globalLogLevel(Debug)
    , m_fileLoggingEnabled(false)
    , m_maxFileSize(10 * 1024 * 1024)  // 10MB
    , m_backupCount(5)
    , m_consoleLoggingEnabled(true)
    , m_totalLogCount(0)
    , m_startTime(QDateTime::currentDateTime())
{
    // 初始化各类型日志级别
    m_typeLevels[General] = Debug;
    m_typeLevels[Performance] = Info;
    m_typeLevels[Memory] = Info;
    m_typeLevels[Network] = Debug;
    m_typeLevels[Threading] = Debug;
    m_typeLevels[Security] = Warning;
    
    // 初始化日志计数
    for (int i = Trace; i <= Fatal; ++i) {
        m_logCounts[static_cast<LogLevel>(i)] = 0;
    }
    
    for (int i = General; i <= Security; ++i) {
        m_typeCounts[static_cast<LogType>(i)] = 0;
    }
    
    // 创建定时器
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(5000);  // 5秒刷新一次
    connect(m_flushTimer, &QTimer::timeout, this, &DebugLogger::periodicFlush);
    
    m_rotationTimer = new QTimer(this);
    m_rotationTimer->setInterval(60000);  // 1分钟检查一次
    connect(m_rotationTimer, &QTimer::timeout, this, &DebugLogger::checkLogRotation);
    
    // 安装Qt消息处理器
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context, const QString& message) {
        if (DebugLogger::s_instance) {
            DebugLogger::s_instance->handleQtMessage(type, context, message);
        }
    });
}

DebugLogger::~DebugLogger()
{
    flush();
    
    if (m_logStream) {
        m_logStream.reset();
    }
    
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
}

DebugLogger* DebugLogger::instance()
{
    QMutexLocker locker(&s_mutex);
    if (!s_instance) {
        s_instance = new DebugLogger();
    }
    return s_instance;
}

bool DebugLogger::initialize(const QString& configFile)
{
    QMutexLocker locker(&m_mutex);
    
    // 加载配置文件
    if (!configFile.isEmpty()) {
        if (!loadConfiguration(configFile)) {
            qWarning() << "Failed to load debug logger configuration from:" << configFile;
        }
    }
    
    // 如果启用了文件日志但没有设置路径，使用默认路径
    if (m_fileLoggingEnabled && m_logFilePath.isEmpty()) {
        QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(appDataPath);
        m_logFilePath = appDataPath + "/debug.log";
    }
    
    // 初始化文件日志
    if (m_fileLoggingEnabled) {
        m_logFile = std::make_unique<QFile>(m_logFilePath);
        if (m_logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
            m_logStream = std::make_unique<QTextStream>(m_logFile.get());
            m_logStream->setEncoding(QStringConverter::Utf8);
            
            // 写入启动标记
            QString startMessage = QString("\n=== Debug Logger Started at %1 ===\n")
                                 .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
            *m_logStream << startMessage;
            m_logStream->flush();
        } else {
            qWarning() << "Failed to open log file:" << m_logFilePath;
            m_fileLoggingEnabled = false;
        }
    }
    
    // 启动定时器
    m_flushTimer->start();
    m_rotationTimer->start();
    
    // 记录初始化信息
    log(Info, "DebugLogger", QString("Debug logger initialized. File logging: %1, Console logging: %2")
        .arg(m_fileLoggingEnabled ? "enabled" : "disabled")
        .arg(m_consoleLoggingEnabled ? "enabled" : "disabled"));
    
    return true;
}

void DebugLogger::setLogLevel(LogLevel level)
{
    QMutexLocker locker(&m_mutex);
    m_globalLogLevel = level;
}

void DebugLogger::setLogLevel(LogType type, LogLevel level)
{
    QMutexLocker locker(&m_mutex);
    m_typeLevels[type] = level;
}

void DebugLogger::enableFileLogging(const QString& filePath, qint64 maxSize, int backupCount)
{
    QMutexLocker locker(&m_mutex);
    
    m_fileLoggingEnabled = true;
    m_logFilePath = filePath;
    m_maxFileSize = maxSize;
    m_backupCount = backupCount;
    
    // 确保目录存在
    QFileInfo fileInfo(filePath);
    QDir().mkpath(fileInfo.absolutePath());
}

void DebugLogger::enableConsoleLogging(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_consoleLoggingEnabled = enabled;
}

void DebugLogger::log(LogLevel level, const QString& category, const QString& message, 
                     const QString& function, int line)
{
    QMutexLocker locker(&m_mutex);
    
    // 检查日志级别
    if (level < m_globalLogLevel) {
        return;
    }
    
    // 更新统计信息
    {
        QMutexLocker statsLocker(&m_statsMutex);
        m_logCounts[level]++;
        m_typeCounts[General]++;
        m_totalLogCount++;
    }
    
    // 格式化消息
    QString formattedMessage = formatMessage(level, General, category, message, function, line);
    
    // 输出到文件
    if (m_fileLoggingEnabled) {
        writeToFile(formattedMessage);
    }
    
    // 输出到控制台
    if (m_consoleLoggingEnabled) {
        writeToConsole(level, formattedMessage);
    }
    
    // 发射信号
    emit logMessage(level, General, formattedMessage);
}

void DebugLogger::logPerformance(const QString& operation, qint64 duration, 
                                const QHash<QString, QVariant>& metrics)
{
    QMutexLocker locker(&m_mutex);
    
    LogLevel level = m_typeLevels[Performance];
    if (level < m_globalLogLevel) {
        return;
    }
    
    // 更新统计信息
    {
        QMutexLocker statsLocker(&m_statsMutex);
        m_logCounts[level]++;
        m_typeCounts[Performance]++;
        m_totalLogCount++;
    }
    
    // 构建性能日志消息
    QString message = QString("PERFORMANCE: %1 took %2ms").arg(operation).arg(duration);
    
    if (!metrics.isEmpty()) {
        QStringList metricsList;
        for (auto it = metrics.constBegin(); it != metrics.constEnd(); ++it) {
            metricsList << QString("%1=%2").arg(it.key()).arg(it.value().toString());
        }
        message += QString(" [%1]").arg(metricsList.join(", "));
    }
    
    QString formattedMessage = formatMessage(level, Performance, "Performance", message, QString(), -1);
    
    // 输出日志
    if (m_fileLoggingEnabled) {
        writeToFile(formattedMessage);
    }
    
    if (m_consoleLoggingEnabled) {
        writeToConsole(level, formattedMessage);
    }
    
    // 发射信号
    emit performanceMetric(operation, duration, metrics);
    emit logMessage(level, Performance, formattedMessage);
}

void DebugLogger::logMemoryUsage(const QString& context, qint64 memoryUsed, qint64 memoryTotal)
{
    QMutexLocker locker(&m_mutex);
    
    LogLevel level = m_typeLevels[Memory];
    if (level < m_globalLogLevel) {
        return;
    }
    
    // 更新统计信息
    {
        QMutexLocker statsLocker(&m_statsMutex);
        m_logCounts[level]++;
        m_typeCounts[Memory]++;
        m_totalLogCount++;
    }
    
    // 构建内存日志消息
    QString message = QString("MEMORY: %1 - Used: %2 bytes")
                     .arg(context)
                     .arg(memoryUsed);
    
    if (memoryTotal > 0) {
        double percentage = (double)memoryUsed / memoryTotal * 100.0;
        message += QString(" / %1 bytes (%.2f%%)").arg(memoryTotal).arg(percentage);
    }
    
    QString formattedMessage = formatMessage(level, Memory, "Memory", message, QString(), -1);
    
    // 输出日志
    if (m_fileLoggingEnabled) {
        writeToFile(formattedMessage);
    }
    
    if (m_consoleLoggingEnabled) {
        writeToConsole(level, formattedMessage);
    }
    
    emit logMessage(level, Memory, formattedMessage);
}

void DebugLogger::logNetworkActivity(const QString& operation, const QString& endpoint, 
                                    qint64 bytesTransferred, qint64 duration)
{
    QMutexLocker locker(&m_mutex);
    
    LogLevel level = m_typeLevels[Network];
    if (level < m_globalLogLevel) {
        return;
    }
    
    // 更新统计信息
    {
        QMutexLocker statsLocker(&m_statsMutex);
        m_logCounts[level]++;
        m_typeCounts[Network]++;
        m_totalLogCount++;
    }
    
    // 构建网络日志消息
    QString message = QString("NETWORK: %1 %2 - %3 bytes in %4ms")
                     .arg(operation)
                     .arg(endpoint)
                     .arg(bytesTransferred)
                     .arg(duration);
    
    if (duration > 0) {
        double throughput = (double)bytesTransferred / duration * 1000.0;  // bytes/sec
        message += QString(" (%.2f bytes/sec)").arg(throughput);
    }
    
    QString formattedMessage = formatMessage(level, Network, "Network", message, QString(), -1);
    
    // 输出日志
    if (m_fileLoggingEnabled) {
        writeToFile(formattedMessage);
    }
    
    if (m_consoleLoggingEnabled) {
        writeToConsole(level, formattedMessage);
    }
    
    emit logMessage(level, Network, formattedMessage);
}

void DebugLogger::logThreadActivity(const QString& threadName, const QString& activity, 
                                   const QHash<QString, QVariant>& details)
{
    QMutexLocker locker(&m_mutex);
    
    LogLevel level = m_typeLevels[Threading];
    if (level < m_globalLogLevel) {
        return;
    }
    
    // 更新统计信息
    {
        QMutexLocker statsLocker(&m_statsMutex);
        m_logCounts[level]++;
        m_typeCounts[Threading]++;
        m_totalLogCount++;
    }
    
    // 构建线程日志消息
    QString message = QString("THREAD: [%1] %2").arg(threadName).arg(activity);
    
    if (!details.isEmpty()) {
        QStringList detailsList;
        for (auto it = details.constBegin(); it != details.constEnd(); ++it) {
            detailsList << QString("%1=%2").arg(it.key()).arg(it.value().toString());
        }
        message += QString(" [%1]").arg(detailsList.join(", "));
    }
    
    QString formattedMessage = formatMessage(level, Threading, "Threading", message, QString(), -1);
    
    // 输出日志
    if (m_fileLoggingEnabled) {
        writeToFile(formattedMessage);
    }
    
    if (m_consoleLoggingEnabled) {
        writeToConsole(level, formattedMessage);
    }
    
    emit logMessage(level, Threading, formattedMessage);
}

QString DebugLogger::startPerformanceTimer(const QString& operationId)
{
    QMutexLocker locker(&m_mutex);
    
    QString timerId = QUuid::createUuid().toString();
    m_performanceTimers[timerId] = QDateTime::currentDateTime();
    
    log(Trace, "Performance", QString("Started timer for operation: %1 (ID: %2)")
        .arg(operationId).arg(timerId));
    
    return timerId;
}

void DebugLogger::endPerformanceTimer(const QString& timerId, const QString& additionalInfo)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_performanceTimers.contains(timerId)) {
        log(Warning, "Performance", QString("Timer ID not found: %1").arg(timerId));
        return;
    }
    
    QDateTime startTime = m_performanceTimers.take(timerId);
    qint64 duration = startTime.msecsTo(QDateTime::currentDateTime());
    
    QString message = QString("Timer %1 completed in %2ms").arg(timerId).arg(duration);
    if (!additionalInfo.isEmpty()) {
        message += QString(" - %1").arg(additionalInfo);
    }
    
    logPerformance(QString("Timer_%1").arg(timerId), duration);
}

QHash<QString, QVariant> DebugLogger::getLogStatistics() const
{
    QMutexLocker statsLocker(&m_statsMutex);
    
    QHash<QString, QVariant> stats;
    
    // 基本统计
    stats["total_logs"] = m_totalLogCount;
    stats["start_time"] = m_startTime;
    stats["uptime_seconds"] = m_startTime.secsTo(QDateTime::currentDateTime());
    
    // 按级别统计
    for (auto it = m_logCounts.constBegin(); it != m_logCounts.constEnd(); ++it) {
        stats[QString("level_%1").arg(logLevelToString(it.key()).toLower())] = it.value();
    }
    
    // 按类型统计
    for (auto it = m_typeCounts.constBegin(); it != m_typeCounts.constEnd(); ++it) {
        stats[QString("type_%1").arg(logTypeToString(it.key()).toLower())] = it.value();
    }
    
    // 文件信息
    if (m_fileLoggingEnabled && m_logFile) {
        stats["log_file_path"] = m_logFilePath;
        stats["log_file_size"] = m_logFile->size();
    }
    
    return stats;
}

void DebugLogger::flush()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_logStream) {
        m_logStream->flush();
    }
    
    if (m_logFile) {
        m_logFile->flush();
    }
}

void DebugLogger::handleQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    LogLevel level;
    switch (type) {
    case QtDebugMsg:
        level = Debug;
        break;
    case QtInfoMsg:
        level = Info;
        break;
    case QtWarningMsg:
        level = Warning;
        break;
    case QtCriticalMsg:
        level = Critical;
        break;
    case QtFatalMsg:
        level = Fatal;
        break;
    default:
        level = Debug;
        break;
    }
    
    QString category = context.category ? QString(context.category) : "Qt";
    QString function = context.function ? QString(context.function) : QString();
    
    log(level, category, message, function, context.line);
}

void DebugLogger::periodicFlush()
{
    flush();
}

void DebugLogger::checkLogRotation()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_fileLoggingEnabled || !m_logFile) {
        return;
    }
    
    if (m_logFile->size() > m_maxFileSize) {
        rotateLogFile();
    }
}

QString DebugLogger::formatMessage(LogLevel level, LogType type, const QString& category, 
                                  const QString& message, const QString& function, int line) const
{
    QDateTime now = QDateTime::currentDateTime();
    QString timestamp = now.toString("yyyy-MM-dd hh:mm:ss.zzz");
    
    QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThread()), 16);
    
    QString levelStr = logLevelToString(level);
    QString typeStr = logTypeToString(type);
    
    QString result = QString("[%1] [%2] [%3] [%4] %5: %6")
                    .arg(timestamp)
                    .arg(threadId)
                    .arg(levelStr)
                    .arg(typeStr)
                    .arg(category)
                    .arg(message);
    
    if (!function.isEmpty() && line > 0) {
        result += QString(" (%1:%2)").arg(function).arg(line);
    }
    
    return result;
}

void DebugLogger::writeToFile(const QString& message)
{
    if (m_logStream) {
        *m_logStream << message << Qt::endl;
    }
}

void DebugLogger::writeToConsole(LogLevel level, const QString& message)
{
    // 根据日志级别选择输出流
    if (level >= Error) {
        std::cerr << message.toStdString() << std::endl;
    } else {
        std::cout << message.toStdString() << std::endl;
    }
}

void DebugLogger::rotateLogFile()
{
    if (!m_logFile) {
        return;
    }
    
    // 关闭当前文件
    m_logStream.reset();
    m_logFile->close();
    
    // 轮转备份文件
    for (int i = m_backupCount - 1; i >= 1; --i) {
        QString oldFile = QString("%1.%2").arg(m_logFilePath).arg(i);
        QString newFile = QString("%1.%2").arg(m_logFilePath).arg(i + 1);
        
        if (QFile::exists(oldFile)) {
            QFile::remove(newFile);
            QFile::rename(oldFile, newFile);
        }
    }
    
    // 移动当前文件为第一个备份
    QString backupFile = QString("%1.1").arg(m_logFilePath);
    QFile::remove(backupFile);
    QFile::rename(m_logFilePath, backupFile);
    
    // 重新打开新文件
    if (m_logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        m_logStream = std::make_unique<QTextStream>(m_logFile.get());
        m_logStream->setEncoding(QStringConverter::Utf8);
        
        QString rotationMessage = QString("\n=== Log file rotated at %1 ===\n")
                                .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        *m_logStream << rotationMessage;
        m_logStream->flush();
    }
}

bool DebugLogger::loadConfiguration(const QString& configFile)
{
    QSettings settings(configFile, QSettings::IniFormat);
    
    if (settings.status() != QSettings::NoError) {
        return false;
    }
    
    // 加载全局设置
    QString globalLevel = settings.value("GLOBAL_LOG_LEVEL", "debug").toString().toLower();
    if (globalLevel == "trace") m_globalLogLevel = Trace;
    else if (globalLevel == "debug") m_globalLogLevel = Debug;
    else if (globalLevel == "info") m_globalLogLevel = Info;
    else if (globalLevel == "warning") m_globalLogLevel = Warning;
    else if (globalLevel == "error") m_globalLogLevel = Error;
    else if (globalLevel == "critical") m_globalLogLevel = Critical;
    else if (globalLevel == "fatal") m_globalLogLevel = Fatal;
    
    // 加载文件日志设置
    m_fileLoggingEnabled = settings.value("LOG_TO_FILE", false).toBool();
    m_logFilePath = settings.value("LOG_FILE_PATH", "").toString();
    
    QString maxSizeStr = settings.value("LOG_FILE_MAX_SIZE", "10MB").toString();
    if (maxSizeStr.endsWith("MB", Qt::CaseInsensitive)) {
        m_maxFileSize = maxSizeStr.left(maxSizeStr.length() - 2).toLongLong() * 1024 * 1024;
    } else if (maxSizeStr.endsWith("KB", Qt::CaseInsensitive)) {
        m_maxFileSize = maxSizeStr.left(maxSizeStr.length() - 2).toLongLong() * 1024;
    } else {
        m_maxFileSize = maxSizeStr.toLongLong();
    }
    
    m_backupCount = settings.value("LOG_FILE_BACKUP_COUNT", 5).toInt();
    
    // 加载控制台日志设置
    m_consoleLoggingEnabled = settings.value("LOG_TO_CONSOLE", true).toBool();
    
    return true;
}

QString DebugLogger::logLevelToString(LogLevel level) const
{
    switch (level) {
    case Trace: return "TRACE";
    case Debug: return "DEBUG";
    case Info: return "INFO";
    case Warning: return "WARN";
    case Error: return "ERROR";
    case Critical: return "CRIT";
    case Fatal: return "FATAL";
    default: return "UNKNOWN";
    }
}

QString DebugLogger::logTypeToString(LogType type) const
{
    switch (type) {
    case General: return "GEN";
    case Performance: return "PERF";
    case Memory: return "MEM";
    case Network: return "NET";
    case Threading: return "THR";
    case Security: return "SEC";
    default: return "UNK";
    }
}