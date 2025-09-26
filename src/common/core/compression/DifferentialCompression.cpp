#include "DifferentialCompression.h"
#include "../logging/LoggingCategories.h"
#include "../compression/Compression.h"
#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <algorithm>

// 默认配置常量
static const double DEFAULT_DIFFERENTIAL_THRESHOLD = 0.8;  // 80%阈值
static const int DEFAULT_MAX_FRAME_CACHE = 5;              // 最多缓存5帧
static const qint64 CACHE_OPTIMIZE_INTERVAL = 30000;       // 30秒优化间隔
static const qint64 PERFORMANCE_WARNING_THRESHOLD = 100;   // 100ms性能警告阈值

DifferentialCompression::DifferentialCompression(QObject *parent)
    : QObject(parent)
    , m_differentialThreshold(DEFAULT_DIFFERENTIAL_THRESHOLD)
    , m_maxFrameCache(DEFAULT_MAX_FRAME_CACHE)
    , m_performanceMonitoring(true)
    , m_currentFrameId(0)
    , m_lastOptimizeTime(QDateTime::currentMSecsSinceEpoch())
    , m_cacheOptimizeInterval(CACHE_OPTIMIZE_INTERVAL)
{
    // 初始化性能统计
    resetPerformanceStats();
    
    qCDebug(lcDiffCompression, "差分压缩管理器初始化完成");
}

DifferentialCompression::~DifferentialCompression()
{
    QMutexLocker locker(&m_mutex);
    m_frameCache.clear();
    qCDebug(lcDiffCompression, "差分压缩管理器销毁");
}

DifferentialCompression::CompressionResult 
DifferentialCompression::compress(const ZeroCopyByteArrayPtr& current, 
                                 const ZeroCopyByteArrayPtr& previous)
{
    if (!current || current.isNull()) {
        CompressionResult result;
        result.errorMessage = "当前数据为空";
        result.success = false;
        return result;
    }

    QByteArray currentData = current->data();
    QByteArray previousData = previous ? previous->data() : QByteArray();
    
    return compress(currentData, previousData);
}

DifferentialCompression::CompressionResult 
DifferentialCompression::compress(const QByteArray& current, const QByteArray& previous)
{
    QElapsedTimer timer;
    timer.start();
    
    CompressionResult result;
    result.originalSize = current.size();
    
    try {
        // 如果没有前一帧数据或数据为空，执行完整压缩
        if (previous.isEmpty() || current.isEmpty()) {
            result = performFullCompression(current);
        } else {
            // 尝试差分压缩
            result = performDifferentialCompression(current, previous);
        }
        
        result.processingTime = timer.nsecsElapsed() / 1000; // 转换为微秒
        result.success = true;
        
        // 更新性能统计
        if (m_performanceMonitoring) {
            updateCompressionStats(result);
            checkPerformanceWarnings();
        }
        
        // 添加到帧缓存
        auto currentPtr = makeZeroCopyByteArray(current);
        addFrameToCache(currentPtr, ++m_currentFrameId);
        
        // 发送完成信号
        emit compressionCompleted(result);
        
        qCDebug(lcDiffCompression, "压缩完成: %lld -> %lld bytes, 压缩比: %.2f%%, 差分: %s",
                result.originalSize, result.compressedSize, result.compressionRatio * 100.0,
                result.isDifferential ? "是" : "否");
        
    } catch (const std::exception& e) {
        result.errorMessage = QString("压缩异常: %1").arg(e.what());
        result.success = false;
        result.processingTime = timer.nsecsElapsed() / 1000;
        
        if (m_performanceMonitoring) {
            QMutexLocker locker(&m_mutex);
            m_stats.compressionErrors++;
        }
        
        qCWarning(lcDiffCompression, "压缩失败:%s", result.errorMessage.toUtf8().constData());
    }
    
    return result;
}

DifferentialCompression::DecompressionResult 
DifferentialCompression::decompress(const ZeroCopyByteArrayPtr& compressed, 
                                   const ZeroCopyByteArrayPtr& previous)
{
    if (!compressed || compressed.isNull() || compressed->data().isEmpty()) {
        DecompressionResult result;
        result.errorMessage = "压缩数据为空";
        result.success = false;
        return result;
    }

    QByteArray compressedData = compressed->data();
    QByteArray previousData = previous ? previous->data() : QByteArray();
    
    return decompress(compressedData, previousData);
}

DifferentialCompression::DecompressionResult 
DifferentialCompression::decompress(const QByteArray& compressed, const QByteArray& previous)
{
    QElapsedTimer timer;
    timer.start();
    
    DecompressionResult result;
    result.attemptCount = 1;
    
    try {
        // 首先尝试差分解压（如果有前一帧数据）
        if (!previous.isEmpty()) {
            result = performDifferentialDecompression(compressed, previous);
            
            // 如果差分解压失败，尝试完整解压
            if (!result.success) {
                qCDebug(lcDiffCompression, "差分解压失败，尝试完整解压");
                result = performFullDecompression(compressed);
                result.usedFallback = true;
                result.attemptCount = 2;
            }
        } else {
            // 直接进行完整解压
            result = performFullDecompression(compressed);
        }
        
        result.processingTime = timer.nsecsElapsed() / 1000; // 转换为微秒
        
        // 更新性能统计
        if (m_performanceMonitoring) {
            updateDecompressionStats(result);
        }
        
        // 发送完成信号
        emit decompressionCompleted(result);
        
        qCDebug(lcDiffCompression, "解压完成: %lld bytes, 尝试次数: %d, 回退: %s",
                result.data ? result.data->dataSize() : 0, result.attemptCount,
                result.usedFallback ? "是" : "否");
        
    } catch (const std::exception& e) {
        result.errorMessage = QString("解压异常: %1").arg(e.what());
        result.success = false;
        result.processingTime = timer.nsecsElapsed() / 1000;
        
        if (m_performanceMonitoring) {
            QMutexLocker locker(&m_mutex);
            m_stats.decompressionErrors++;
        }
        
        qCWarning(lcDiffCompression, "解压失败:%s", result.errorMessage.toUtf8().constData());
    }
    
    return result;
}

void DifferentialCompression::setDifferentialThreshold(double threshold)
{
    QMutexLocker locker(&m_mutex);
    m_differentialThreshold = qBound(0.0, threshold, 1.0);
    qCDebug(lcDiffCompression, "差分压缩阈值设置为:%.2f", m_differentialThreshold);
}

double DifferentialCompression::differentialThreshold() const
{
    QMutexLocker locker(&m_mutex);
    return m_differentialThreshold;
}

void DifferentialCompression::setMaxFrameCache(int maxFrames)
{
    QMutexLocker locker(&m_mutex);
    m_maxFrameCache = qMax(1, maxFrames);
    
    // 如果当前缓存超过新的限制，清理多余的帧
    while (m_frameCache.size() > m_maxFrameCache) {
        m_frameCache.dequeue();
    }
    
    qCDebug(lcDiffCompression, "最大帧缓存设置为:%d", m_maxFrameCache);
}

int DifferentialCompression::maxFrameCache() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxFrameCache;
}

void DifferentialCompression::clearFrameCache()
{
    QMutexLocker locker(&m_mutex);
    m_frameCache.clear();
    m_currentFrameId = 0;
    qCDebug(lcDiffCompression, "帧缓存已清空");
}

DifferentialCompression::PerformanceStats DifferentialCompression::getPerformanceStats() const
{
    QMutexLocker locker(&m_mutex);
    return m_stats;
}

void DifferentialCompression::resetPerformanceStats()
{
    QMutexLocker locker(&m_mutex);
    m_stats = PerformanceStats();
    qCDebug(lcDiffCompression, "性能统计已重置");
}

void DifferentialCompression::setPerformanceMonitoring(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_performanceMonitoring = enabled;
    qCDebug(lcDiffCompression, "性能监控%s", enabled ? "启用" : "禁用");
}

bool DifferentialCompression::isPerformanceMonitoringEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_performanceMonitoring;
}

double DifferentialCompression::getCompressionEfficiency() const
{
    QMutexLocker locker(&m_mutex);
    if (m_stats.totalCompressions == 0) {
        return 0.0;
    }
    
    return (static_cast<double>(m_stats.differentialCompressions) / m_stats.totalCompressions) * 100.0;
}

int DifferentialCompression::cachedFrameCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_frameCache.size();
}

void DifferentialCompression::optimizeCache()
{
    QMutexLocker locker(&m_mutex);
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    if (currentTime - m_lastOptimizeTime < m_cacheOptimizeInterval) {
        return; // 还未到优化时间
    }
    
    int originalSize = m_frameCache.size();
    
    // 清理过期的帧（超过10秒的帧）
    qint64 expireTime = currentTime - 10000;
    while (!m_frameCache.isEmpty() && m_frameCache.head().timestamp < expireTime) {
        m_frameCache.dequeue();
    }
    
    m_lastOptimizeTime = currentTime;
    
    int cleanedFrames = originalSize - m_frameCache.size();
    if (cleanedFrames > 0) {
        qCDebug(lcDiffCompression, "缓存优化完成，清理了%d个过期帧", cleanedFrames);
    }
}

DifferentialCompression::CompressionResult 
DifferentialCompression::performDifferentialCompression(const QByteArray& current, const QByteArray& previous)
{
    CompressionResult result;
    result.originalSize = current.size();
    
    // 调用现有的差分压缩函数
    QByteArray compressed = Compression::compressDifference(current, previous);
    
    result.compressedSize = compressed.size();
    result.compressionRatio = 1.0 - (static_cast<double>(result.compressedSize) / result.originalSize);
    
    // 检查是否应该使用差分压缩
    if (result.compressionRatio < (1.0 - m_differentialThreshold)) {
        // 差分压缩效果不好，使用完整压缩
        qCDebug(lcDiffCompression, "差分压缩效果不佳，回退到完整压缩");
        return performFullCompression(current);
    }
    
    result.data = makeZeroCopyByteArray(compressed);
    result.isDifferential = true;
    result.success = true;
    
    return result;
}

DifferentialCompression::CompressionResult 
DifferentialCompression::performFullCompression(const QByteArray& data)
{
    CompressionResult result;
    result.originalSize = data.size();
    
    // 使用标准压缩（这里可以集成其他压缩算法）
    QByteArray compressed = qCompress(data, 6); // 使用中等压缩级别
    
    result.compressedSize = compressed.size();
    result.compressionRatio = 1.0 - (static_cast<double>(result.compressedSize) / result.originalSize);
    result.data = makeZeroCopyByteArray(compressed);
    result.isDifferential = false;
    result.success = true;
    
    return result;
}

DifferentialCompression::DecompressionResult 
DifferentialCompression::performDifferentialDecompression(const QByteArray& compressed, const QByteArray& previous)
{
    DecompressionResult result;
    
    try {
        // 调用现有的差分解压函数
        QByteArray decompressed = Compression::applyDifference(previous, compressed);
        
        if (!decompressed.isEmpty()) {
            result.data = makeZeroCopyByteArray(decompressed);
            result.success = true;
        } else {
            result.errorMessage = "差分解压返回空数据";
            result.success = false;
        }
        
    } catch (const std::exception& e) {
        result.errorMessage = QString("差分解压异常: %1").arg(e.what());
        result.success = false;
    }
    
    return result;
}

DifferentialCompression::DecompressionResult 
DifferentialCompression::performFullDecompression(const QByteArray& compressed)
{
    DecompressionResult result;
    
    try {
        // 使用标准解压
        QByteArray decompressed = qUncompress(compressed);
        
        if (!decompressed.isEmpty()) {
            result.data = makeZeroCopyByteArray(decompressed);
            result.success = true;
        } else {
            result.errorMessage = "标准解压返回空数据";
            result.success = false;
        }
        
    } catch (const std::exception& e) {
        result.errorMessage = QString("标准解压异常: %1").arg(e.what());
        result.success = false;
    }
    
    return result;
}

void DifferentialCompression::addFrameToCache(const ZeroCopyByteArrayPtr& data, quint32 frameId)
{
    QMutexLocker locker(&m_mutex);
    
    FrameCacheItem item;
    item.data = data;
    item.timestamp = QDateTime::currentMSecsSinceEpoch();
    item.frameId = frameId;
    item.dataSize = data ? data->dataSize() : 0;
    
    m_frameCache.enqueue(item);
    
    // 保持缓存大小在限制内
    while (m_frameCache.size() > m_maxFrameCache) {
        m_frameCache.dequeue();
    }
}

ZeroCopyByteArrayPtr DifferentialCompression::getFrameFromCache(quint32 frameId) const
{
    QMutexLocker locker(&m_mutex);
    
    for (const auto& item : m_frameCache) {
        if (item.frameId == frameId) {
            return item.data;
        }
    }
    
    return ZeroCopyByteArrayPtr();
}

void DifferentialCompression::updateCompressionStats(const CompressionResult& result)
{
    QMutexLocker locker(&m_mutex);
    
    m_stats.totalCompressions++;
    
    if (result.isDifferential) {
        m_stats.differentialCompressions++;
    } else {
        m_stats.fullCompressions++;
    }
    
    m_stats.totalOriginalBytes += result.originalSize;
    m_stats.totalCompressedBytes += result.compressedSize;
    m_stats.totalSavedBytes += (result.originalSize - result.compressedSize);
    
    // 更新平均压缩比
    double totalRatio = m_stats.averageCompressionRatio * (m_stats.totalCompressions - 1) + result.compressionRatio;
    m_stats.averageCompressionRatio = totalRatio / m_stats.totalCompressions;
    
    // 更新平均压缩时间（转换为毫秒）
    double totalTime = m_stats.averageCompressionTime * (m_stats.totalCompressions - 1) + (result.processingTime / 1000.0);
    m_stats.averageCompressionTime = totalTime / m_stats.totalCompressions;
    
    if (!result.success) {
        m_stats.compressionErrors++;
    }
}

void DifferentialCompression::updateDecompressionStats(const DecompressionResult& result)
{
    QMutexLocker locker(&m_mutex);
    
    m_stats.totalDecompressions++;
    
    if (result.success) {
        m_stats.successfulDecompressions++;
    } else {
        m_stats.decompressionErrors++;
    }
    
    if (result.usedFallback) {
        m_stats.fallbackDecompressions++;
    }
    
    // 更新平均解压时间（转换为毫秒）
    double totalTime = m_stats.averageDecompressionTime * (m_stats.totalDecompressions - 1) + (result.processingTime / 1000.0);
    m_stats.averageDecompressionTime = totalTime / m_stats.totalDecompressions;
}

void DifferentialCompression::checkPerformanceWarnings()
{
    QMutexLocker locker(&m_mutex);
    
    // 检查压缩时间警告
    if (m_stats.averageCompressionTime > PERFORMANCE_WARNING_THRESHOLD) {
        emit performanceWarning(QString("平均压缩时间过长: %1ms").arg(m_stats.averageCompressionTime, 0, 'f', 2));
    }
    
    // 检查解压时间警告
    if (m_stats.averageDecompressionTime > PERFORMANCE_WARNING_THRESHOLD) {
        emit performanceWarning(QString("平均解压时间过长: %1ms").arg(m_stats.averageDecompressionTime, 0, 'f', 2));
    }
    
    // 检查错误率警告
    if (m_stats.totalCompressions > 100) {
        double errorRate = static_cast<double>(m_stats.compressionErrors) / m_stats.totalCompressions;
        if (errorRate > 0.05) { // 5%错误率
            emit performanceWarning(QString("压缩错误率过高: %1%").arg(errorRate * 100, 0, 'f', 1));
        }
    }
    
    // 检查差分压缩效率
    if (m_stats.totalCompressions > 50) {
        double diffEfficiency = getCompressionEfficiency();
        if (diffEfficiency < 30.0) { // 差分压缩使用率低于30%
            emit performanceWarning(QString("差分压缩效率较低: %1%").arg(diffEfficiency, 0, 'f', 1));
        }
    }
}