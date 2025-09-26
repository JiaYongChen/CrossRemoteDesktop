#include "StorageManager.h"
#include <QtCore/QStandardPaths>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QFileInfo>
#include <QtCore/QDirIterator>
#include <QtCore/QMutexLocker>
#include <QtCore/QCoreApplication>
#include <QtCore/QDataStream>
#include <QtCore/QBuffer>

Q_LOGGING_CATEGORY(lcStorageManager, "server.storage")

StorageManager::StorageManager(QObject* parent)
    : QObject(parent)
    , m_cleanupTimer(new QTimer(this))
    , m_statsTimer(new QTimer(this))
{
    qCDebug(lcStorageManager) << "StorageManager 构造函数调用";
    
    // 设置定时器
    m_cleanupTimer->setSingleShot(false);
    m_cleanupTimer->setInterval(CLEANUP_INTERVAL_MS);
    connect(m_cleanupTimer, &QTimer::timeout, this, &StorageManager::performPeriodicCleanup);
    
    m_statsTimer->setSingleShot(false);
    m_statsTimer->setInterval(STATS_UPDATE_INTERVAL_MS);
    connect(m_statsTimer, &QTimer::timeout, this, &StorageManager::checkStorageUsage);
}

StorageManager::~StorageManager()
{
    qCDebug(lcStorageManager) << "StorageManager 析构函数调用";
    
    if (m_cleanupTimer->isActive()) {
        m_cleanupTimer->stop();
    }
    if (m_statsTimer->isActive()) {
        m_statsTimer->stop();
    }
}

bool StorageManager::initialize(const StorageConfig& config)
{
    QMutexLocker locker(&m_mutex);
    
    qCDebug(lcStorageManager) << "初始化存储管理器，策略:" << static_cast<int>(config.policy);
    
    m_config = config;
    
    // 设置默认存储路径
    if (m_config.storageBasePath.isEmpty()) {
        m_config.storageBasePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/storage";
    }
    
    // 初始化存储目录
    if (!initializeStorageDirectories()) {
        qCWarning(lcStorageManager) << "初始化存储目录失败";
        return false;
    }
    
    // 启动定时器
    if (m_config.policy != StoragePolicy::None) {
        m_cleanupTimer->start();
        m_statsTimer->start();
    }
    
    qCDebug(lcStorageManager) << "存储管理器初始化成功，存储路径:" << m_config.storageBasePath;
    return true;
}

bool StorageManager::storeFrame(const DataRecord& record, bool isKeyFrame)
{
    if (m_config.policy == StoragePolicy::None) {
        return true; // 不存储策略，直接返回成功
    }
    
    if (!shouldStoreFrame(record, isKeyFrame)) {
        return true; // 不需要存储，返回成功
    }
    
    QMutexLocker locker(&m_mutex);
    
    // 生成文件路径
    QString filePath = generateFrameFilePath(record.id, isKeyFrame);
    
    // 保存帧数据
    if (!saveFrameToFile(record, filePath)) {
        qCWarning(lcStorageManager) << "保存帧数据失败:" << record.id;
        return false;
    }
    
    // 更新统计信息
    m_stats.totalStoredFrames++;
    m_stats.totalStorageBytes += record.payload.size();
    
    if (isKeyFrame) {
        m_stats.keyFrameCount++;
        m_lastKeyFrameTime = record.timestamp;
    } else {
        m_stats.deltaFrameCount++;
    }
    
    // 更新时间范围
    if (m_stats.oldestFrameTime.isNull() || record.timestamp < m_stats.oldestFrameTime) {
        m_stats.oldestFrameTime = record.timestamp;
    }
    if (m_stats.newestFrameTime.isNull() || record.timestamp > m_stats.newestFrameTime) {
        m_stats.newestFrameTime = record.timestamp;
    }
    
    // 计算平均帧大小
    if (m_stats.totalStoredFrames > 0) {
        m_stats.averageFrameSize = static_cast<double>(m_stats.totalStorageBytes) / m_stats.totalStoredFrames;
    }
    
    qCDebug(lcStorageManager) << "帧数据存储成功:" << record.id << "关键帧:" << isKeyFrame;
    return true;
}

bool StorageManager::retrieveFrame(const QString& frameId, DataRecord& record)
{
    QMutexLocker locker(&m_mutex);
    
    // 尝试从关键帧目录加载
    QString keyFramePath = generateFrameFilePath(frameId, true);
    if (QFileInfo::exists(keyFramePath)) {
        return loadFrameFromFile(keyFramePath, record);
    }
    
    // 尝试从差分帧目录加载
    QString deltaFramePath = generateFrameFilePath(frameId, false);
    if (QFileInfo::exists(deltaFramePath)) {
        return loadFrameFromFile(deltaFramePath, record);
    }
    
    qCWarning(lcStorageManager) << "未找到帧数据:" << frameId;
    return false;
}

QStringList StorageManager::getStoredFrameIds(const QDateTime& from, const QDateTime& to)
{
    QMutexLocker locker(&m_mutex);
    
    QStringList frameIds;
    
    // 搜索关键帧目录
    QDirIterator keyFrameIter(m_keyFramePath, QStringList() << "*.frame", QDir::Files);
    while (keyFrameIter.hasNext()) {
        keyFrameIter.next();
        QFileInfo fileInfo = keyFrameIter.fileInfo();
        
        if (fileInfo.birthTime() >= from && fileInfo.birthTime() <= to) {
            QString frameId = fileInfo.baseName();
            frameIds.append(frameId);
        }
    }
    
    // 搜索差分帧目录
    QDirIterator deltaFrameIter(m_deltaFramePath, QStringList() << "*.frame", QDir::Files);
    while (deltaFrameIter.hasNext()) {
        deltaFrameIter.next();
        QFileInfo fileInfo = deltaFrameIter.fileInfo();
        
        if (fileInfo.birthTime() >= from && fileInfo.birthTime() <= to) {
            QString frameId = fileInfo.baseName();
            if (!frameIds.contains(frameId)) {
                frameIds.append(frameId);
            }
        }
    }
    
    frameIds.sort();
    return frameIds;
}

StorageStats StorageManager::getStorageStatistics()
{
    QMutexLocker locker(&m_mutex);
    
    // 更新存储大小统计
    m_stats.totalStorageBytes = calculateDirectorySize(m_frameStoragePath);
    
    return m_stats;
}

void StorageManager::cleanupExpiredData()
{
    QMutexLocker locker(&m_mutex);
    
    qCDebug(lcStorageManager) << "开始清理过期数据，保留天数:" << m_config.retentionDays;
    
    int cleanedFrames = 0;
    qint64 freedBytes = 0;
    
    // 清理关键帧
    QDir keyFrameDir(m_keyFramePath);
    int cleanedKeyFrames = cleanupOldFiles(keyFrameDir, m_config.retentionDays);
    cleanedFrames += cleanedKeyFrames;
    
    // 清理差分帧
    QDir deltaFrameDir(m_deltaFramePath);
    int cleanedDeltaFrames = cleanupOldFiles(deltaFrameDir, m_config.retentionDays);
    cleanedFrames += cleanedDeltaFrames;
    
    // 清理诊断数据
    QDir diagnosticDir(m_diagnosticPath);
    cleanupOldFiles(diagnosticDir, m_config.retentionDays);
    
    // 更新统计信息
    qint64 currentSize = calculateDirectorySize(m_frameStoragePath);
    freedBytes = m_stats.totalStorageBytes - currentSize;
    m_stats.totalStorageBytes = currentSize;
    m_stats.totalStoredFrames -= cleanedFrames;
    
    if (cleanedFrames > 0) {
        qCDebug(lcStorageManager) << "清理完成，删除帧数:" << cleanedFrames << "释放空间:" << freedBytes << "字节";
        emit dataCleanupCompleted(cleanedFrames, static_cast<int>(freedBytes / (1024 * 1024)));
    }
}

StorageManager::StorageConfig StorageManager::getCurrentConfig() const
{
    QMutexLocker locker(&m_mutex);
    return m_config;
}

void StorageManager::updateConfig(const StorageConfig& config)
{
    QMutexLocker locker(&m_mutex);
    
    qCDebug(lcStorageManager) << "更新存储配置";
    
    StoragePolicy oldPolicy = m_config.policy;
    m_config = config;
    
    // 如果策略发生变化，重新启动或停止定时器
    if (oldPolicy != config.policy) {
        if (config.policy == StoragePolicy::None) {
            m_cleanupTimer->stop();
            m_statsTimer->stop();
        } else if (oldPolicy == StoragePolicy::None) {
            m_cleanupTimer->start();
            m_statsTimer->start();
        }
    }
}

void StorageManager::collectPerformanceData(const QString& operation, qint64 durationMs, const QJsonObject& metadata)
{
    if (!m_config.enableDiagnostics) {
        return;
    }
    
    QJsonObject perfData;
    perfData["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    perfData["operation"] = operation;
    perfData["duration_ms"] = durationMs;
    perfData["metadata"] = metadata;
    
    saveDiagnosticData("performance", perfData);
    
    qCDebug(lcStorageManager) << "收集性能数据:" << operation << "耗时:" << durationMs << "ms";
}

void StorageManager::collectErrorData(const QString& error, const QString& context, const QString& severity)
{
    if (!m_config.enableDiagnostics) {
        return;
    }
    
    QJsonObject errorData;
    errorData["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    errorData["error"] = error;
    errorData["context"] = context;
    errorData["severity"] = severity;
    
    saveDiagnosticData("error", errorData);
    
    qCDebug(lcStorageManager) << "收集错误数据:" << error << "上下文:" << context;
}

QJsonObject StorageManager::generatePerformanceReport(const QDateTime& from, const QDateTime& to)
{
    QJsonObject report;
    report["type"] = "performance_report";
    report["from"] = from.toString(Qt::ISODate);
    report["to"] = to.toString(Qt::ISODate);
    report["generated_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    
    // 这里应该从诊断数据文件中读取和分析数据
    // 为了简化，返回基本统计信息
    QJsonObject stats;
    stats["total_operations"] = 0;
    stats["average_duration_ms"] = 0.0;
    stats["max_duration_ms"] = 0;
    stats["min_duration_ms"] = 0;
    
    report["statistics"] = stats;
    
    return report;
}

QJsonObject StorageManager::generateErrorReport(const QDateTime& from, const QDateTime& to)
{
    QJsonObject report;
    report["type"] = "error_report";
    report["from"] = from.toString(Qt::ISODate);
    report["to"] = to.toString(Qt::ISODate);
    report["generated_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    
    // 这里应该从诊断数据文件中读取和分析错误数据
    QJsonObject stats;
    stats["total_errors"] = 0;
    stats["error_rate"] = 0.0;
    stats["critical_errors"] = 0;
    stats["warning_errors"] = 0;
    
    report["statistics"] = stats;
    
    return report;
}

void StorageManager::performPeriodicCleanup()
{
    qCDebug(lcStorageManager) << "执行定期清理";
    cleanupExpiredData();
}

void StorageManager::forceCleanup()
{
    qCDebug(lcStorageManager) << "执行强制清理";
    
    // 强制清理策略：删除一半的旧数据
    QMutexLocker locker(&m_mutex);
    
    // 清理超过保留期一半时间的数据
    int aggressiveRetentionDays = m_config.retentionDays / 2;
    if (aggressiveRetentionDays < 1) {
        aggressiveRetentionDays = 1;
    }
    
    QDir keyFrameDir(m_keyFramePath);
    cleanupOldFiles(keyFrameDir, aggressiveRetentionDays);
    
    QDir deltaFrameDir(m_deltaFramePath);
    cleanupOldFiles(deltaFrameDir, aggressiveRetentionDays);
}

void StorageManager::checkStorageUsage()
{
    QMutexLocker locker(&m_mutex);
    
    qint64 currentSize = calculateDirectorySize(m_frameStoragePath);
    qint64 limitBytes = static_cast<qint64>(m_config.maxStorageMB) * 1024 * 1024;
    
    m_stats.totalStorageBytes = currentSize;
    
    if (currentSize > limitBytes * STORAGE_WARNING_THRESHOLD) {
        int usedMB = static_cast<int>(currentSize / (1024 * 1024));
        qCWarning(lcStorageManager) << "存储空间不足，已使用:" << usedMB << "MB，限制:" << m_config.maxStorageMB << "MB";
        emit storageSpaceLow(usedMB, m_config.maxStorageMB);
        
        // 如果超过限制，执行强制清理
        if (currentSize > limitBytes) {
            forceCleanup();
        }
    }
}

bool StorageManager::initializeStorageDirectories()
{
    QDir baseDir(m_config.storageBasePath);
    if (!baseDir.exists() && !baseDir.mkpath(".")) {
        qCWarning(lcStorageManager) << "创建基础存储目录失败:" << m_config.storageBasePath;
        return false;
    }
    
    // 设置子目录路径
    m_frameStoragePath = m_config.storageBasePath + "/frames";
    m_keyFramePath = m_frameStoragePath + "/keyframes";
    m_deltaFramePath = m_frameStoragePath + "/deltaframes";
    m_diagnosticPath = m_config.storageBasePath + "/diagnostics";
    
    // 创建子目录
    QStringList directories = {m_frameStoragePath, m_keyFramePath, m_deltaFramePath, m_diagnosticPath};
    
    for (const QString& dirPath : directories) {
        QDir dir(dirPath);
        if (!dir.exists() && !dir.mkpath(".")) {
            qCWarning(lcStorageManager) << "创建存储目录失败:" << dirPath;
            return false;
        }
    }
    
    qCDebug(lcStorageManager) << "存储目录初始化成功";
    return true;
}

QString StorageManager::generateFrameFilePath(const QString& frameId, bool isKeyFrame)
{
    QString directory = isKeyFrame ? m_keyFramePath : m_deltaFramePath;
    return directory + "/" + frameId + ".frame";
}

bool StorageManager::saveFrameToFile(const DataRecord& record, const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcStorageManager) << "无法打开文件进行写入:" << filePath;
        return false;
    }
    
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    
    // 写入数据记录
    stream << record.id;
    stream << record.timestamp;
    stream << record.mimeType;
    stream << record.payload;
    stream << record.size;
    stream << record.checksum;
    
    if (stream.status() != QDataStream::Ok) {
        qCWarning(lcStorageManager) << "写入帧数据失败:" << filePath;
        return false;
    }
    
    return true;
}

bool StorageManager::loadFrameFromFile(const QString& filePath, DataRecord& record)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcStorageManager) << "无法打开文件进行读取:" << filePath;
        return false;
    }
    
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    
    // 读取数据记录
    stream >> record.id;
    stream >> record.timestamp;
    stream >> record.mimeType;
    stream >> record.payload;
    stream >> record.size;
    stream >> record.checksum;
    
    if (stream.status() != QDataStream::Ok) {
        qCWarning(lcStorageManager) << "读取帧数据失败:" << filePath;
        return false;
    }
    
    return true;
}

qint64 StorageManager::calculateDirectorySize(const QString& dirPath)
{
    qint64 totalSize = 0;
    
    QDirIterator iter(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (iter.hasNext()) {
        iter.next();
        totalSize += iter.fileInfo().size();
    }
    
    return totalSize;
}

int StorageManager::cleanupOldFiles(const QDir& directory, int maxAge)
{
    int cleanedCount = 0;
    QDateTime cutoffTime = QDateTime::currentDateTime().addDays(-maxAge);
    
    QFileInfoList files = directory.entryInfoList(QDir::Files);
    for (const QFileInfo& fileInfo : files) {
        if (fileInfo.birthTime() < cutoffTime) {
            if (QFile::remove(fileInfo.absoluteFilePath())) {
                cleanedCount++;
                qCDebug(lcStorageManager) << "删除过期文件:" << fileInfo.fileName();
            } else {
                qCWarning(lcStorageManager) << "删除文件失败:" << fileInfo.absoluteFilePath();
            }
        }
    }
    
    return cleanedCount;
}

bool StorageManager::shouldStoreFrame(const DataRecord& record, bool isKeyFrame)
{
    Q_UNUSED(record) // 标记参数为未使用，避免编译警告
    
    switch (m_config.policy) {
    case StoragePolicy::None:
        return false;
        
    case StoragePolicy::KeyFramesOnly:
        return isKeyFrame;
        
    case StoragePolicy::RecentFrames:
        // 检查是否在最近帧数量限制内
        return true; // 简化实现，实际应该检查帧数量
        
    case StoragePolicy::FullSession:
        return true;
        
    case StoragePolicy::Diagnostic:
        return isKeyFrame; // 诊断模式只存储关键帧
    }
    
    return false;
}

void StorageManager::saveDiagnosticData(const QString& type, const QJsonObject& data)
{
    QString fileName = QString("%1/%2_%3.json")
                      .arg(m_diagnosticPath)
                      .arg(type)
                      .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(data);
        file.write(doc.toJson());
    } else {
        qCWarning(lcStorageManager) << "保存诊断数据失败:" << fileName;
    }
}