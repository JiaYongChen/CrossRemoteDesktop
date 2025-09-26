#include "AdvancedCompressionManager.h"
#include "../logging/LoggingCategories.h"
#include "../config/MessageConstants.h"
#include "Compression.h"

#include <QtCore/QDebug>
#include <QtCore/QMutexLocker>
#include <QtCore/QElapsedTimer>
#include <QtCore/QBuffer>
#include <QtGui/QImageWriter>
#include <QtGui/QImageReader>
#include <cmath>
#include <algorithm>

AdvancedCompressionManager::AdvancedCompressionManager(QObject *parent)
    : QObject(parent)
    , m_strategy(AdaptiveStrategy)
    , m_changeDetectionMode(HybridLevel)
    , m_statsMutex()
    , m_historyMutex()
    , m_performanceTimer(nullptr)
    , m_lastCompressionTime(0)
    , m_lastCompressionRatio(0.0)
    , m_performanceUpdateCounter(0)
    , m_adaptiveThreshold(DEFAULT_CHANGE_THRESHOLD)
    , m_adaptiveUpdateInterval(DEFAULT_PERFORMANCE_UPDATE_INTERVAL)
{
    // 创建性能监控定时器
    m_performanceTimer = new QTimer(this);
    connect(m_performanceTimer, &QTimer::timeout, this, &AdvancedCompressionManager::onPerformanceTimer);
    
    // 初始化默认配置
    initializeDefaults();
    
    qCInfo(lcCompression, "AdvancedCompressionManager initialized with adaptive strategy");
}

AdvancedCompressionManager::~AdvancedCompressionManager()
{
    if (m_performanceTimer && m_performanceTimer->isActive()) {
        m_performanceTimer->stop();
    }
    
    qCInfo(lcCompression, "AdvancedCompressionManager destroyed");
}

void AdvancedCompressionManager::initializeDefaults()
{
    // 初始化自适应配置
    m_adaptiveConfig.enableAdaptiveStrategy = true;
    m_adaptiveConfig.enableChangeDetection = true;
    m_adaptiveConfig.enablePerformanceMonitoring = true;
    m_adaptiveConfig.maxFrameHistory = DEFAULT_MAX_FRAME_HISTORY;
    m_adaptiveConfig.changeThreshold = DEFAULT_CHANGE_THRESHOLD;
    m_adaptiveConfig.blockSize = DEFAULT_BLOCK_SIZE;
    m_adaptiveConfig.performanceUpdateInterval = DEFAULT_PERFORMANCE_UPDATE_INTERVAL;
    
    // 初始化统计信息
    resetStats();
    
    // 启动性能监控定时器
    if (m_adaptiveConfig.enablePerformanceMonitoring) {
        m_performanceTimer->start(m_adaptiveConfig.performanceUpdateInterval);
    }
}

void AdvancedCompressionManager::setCompressionStrategy(CompressionStrategy strategy)
{
    if (m_strategy != strategy) {
        m_strategy = strategy;
        qCInfo(lcCompression, "Compression strategy changed to: %d", static_cast<int>(strategy));
        emit compressionStrategyChanged(strategy);
    }
}

AdvancedCompressionManager::CompressionStrategy AdvancedCompressionManager::compressionStrategy() const
{
    return m_strategy;
}

void AdvancedCompressionManager::setChangeDetectionMode(ChangeDetectionMode mode)
{
    m_changeDetectionMode = mode;
    qCInfo(lcCompression, "Change detection mode set to: %d", static_cast<int>(mode));
}

AdvancedCompressionManager::ChangeDetectionMode AdvancedCompressionManager::changeDetectionMode() const
{
    return m_changeDetectionMode;
}

void AdvancedCompressionManager::setAdaptiveConfig(const AdaptiveConfig &config)
{
    m_adaptiveConfig = config;
    
    // 更新性能监控定时器
    if (config.enablePerformanceMonitoring && !m_performanceTimer->isActive()) {
        m_performanceTimer->start(config.performanceUpdateInterval);
    } else if (!config.enablePerformanceMonitoring && m_performanceTimer->isActive()) {
        m_performanceTimer->stop();
    } else if (m_performanceTimer->isActive()) {
        m_performanceTimer->setInterval(config.performanceUpdateInterval);
    }
    
    qCInfo(lcCompression, "Adaptive configuration updated");
}

AdvancedCompressionManager::AdaptiveConfig AdvancedCompressionManager::adaptiveConfig() const
{
    return m_adaptiveConfig;
}

QByteArray AdvancedCompressionManager::compressFrame(const QImage &frame, const QString &/*frameId*/)
{
    if (frame.isNull()) {
        qCWarning(lcCompression, "Attempted to compress null frame");
        return QByteArray();
    }
    
    m_compressionTimer.start();
    
    // 选择最优的图像格式和质量
    Compression::ImageFormat format = selectOptimalImageFormat(frame, m_strategy);
    int quality = selectOptimalQuality(frame, format, m_strategy);
    
    // 压缩图像
    QByteArray compressedImageData = Compression::compressImage(frame, format, quality);
    
    // 创建带有帧类型标识的完整数据
    QByteArray compressedData;
    QDataStream stream(&compressedData, QIODevice::WriteOnly);
    quint8 frameType = 0; // 0表示完整帧
    stream << frameType;
    compressedData.append(compressedImageData);
    
    qint64 compressionTime = m_compressionTimer.elapsed();
    
    // 更新统计信息
    QByteArray originalData;
    QBuffer buffer(&originalData);
    buffer.open(QIODevice::WriteOnly);
    frame.save(&buffer, "PNG"); // 使用PNG作为原始大小参考
    
    updateCompressionStats(originalData.size(), compressedData.size(), compressionTime, false);
    
    // 添加到帧历史
    QMutexLocker historyLocker(&m_historyMutex);
    m_frameHistory.push_back(frame);
    if (m_frameHistory.size() > static_cast<size_t>(m_adaptiveConfig.maxFrameHistory)) {
        m_frameHistory.pop_front();
    }
    
    qCDebug(lcCompression, "Frame compressed: %lld bytes -> %lld bytes (%.2f%% ratio) in %lld ms",
            originalData.size(), compressedData.size(),
            (1.0 - static_cast<double>(compressedData.size()) / originalData.size()) * 100.0,
            compressionTime);
    
    return compressedData;
}

QByteArray AdvancedCompressionManager::compressFrameDifferential(const QImage &currentFrame, const QImage &previousFrame, const QString &frameId)
{
    if (currentFrame.isNull()) {
        qCWarning(lcCompression, "Attempted to compress null current frame");
        return QByteArray();
    }
    
    if (previousFrame.isNull()) {
        qCDebug(lcCompression, "No previous frame available, using full frame compression");
        return compressFrame(currentFrame, frameId);
    }
    
    m_compressionTimer.start();
    
    // 检测变化区域
    QList<ChangeRegion> changes;
    if (m_adaptiveConfig.enableChangeDetection) {
        changes = detectChanges(currentFrame, previousFrame);
    }
    
    QByteArray compressedData;
    
    // 如果变化很小，使用差分压缩
    double similarity = calculateFrameSimilarity(currentFrame, previousFrame);
    if (similarity > (1.0 - m_adaptiveConfig.changeThreshold)) {
        // 计算差分数据
        QByteArray currentData, previousData;
        QBuffer currentBuffer(&currentData), previousBuffer(&previousData);
        
        currentBuffer.open(QIODevice::WriteOnly);
        previousBuffer.open(QIODevice::WriteOnly);
        
        currentFrame.save(&currentBuffer, "PNG");
        previousFrame.save(&previousBuffer, "PNG");
        
        compressedData = Compression::compressDifference(currentData, previousData);
        
        // 如果差分压缩效果不好，回退到完整帧压缩
        // 获取完整帧压缩的原始数据（不带frameType标识符）
        Compression::ImageFormat format = selectOptimalImageFormat(currentFrame, m_strategy);
        int quality = selectOptimalQuality(currentFrame, format, m_strategy);
        QByteArray fullCompressedRaw = Compression::compressImage(currentFrame, format, quality);
        
        if (compressedData.size() >= fullCompressedRaw.size() * 0.8) {
            // 使用完整帧压缩，添加frameType标识符
            QByteArray markedData;
            QDataStream stream(&markedData, QIODevice::WriteOnly);
            stream << static_cast<quint8>(0); // 完整帧标记
            markedData.append(fullCompressedRaw);
            compressedData = markedData;
            qCDebug(lcCompression, "Differential compression not efficient, using full frame");
        } else {
            // 标记为差分数据
            QByteArray markedData;
            QDataStream stream(&markedData, QIODevice::WriteOnly);
            stream << static_cast<quint8>(1); // 差分标记
            markedData.append(compressedData);
            compressedData = markedData;
        }
    } else {
        // 变化较大，使用完整帧压缩
        // 获取完整帧压缩的原始数据（不带frameType标识符）
        Compression::ImageFormat format = selectOptimalImageFormat(currentFrame, m_strategy);
        int quality = selectOptimalQuality(currentFrame, format, m_strategy);
        QByteArray fullCompressedRaw = Compression::compressImage(currentFrame, format, quality);
        
        // 标记为完整帧数据
        QByteArray markedData;
        QDataStream stream(&markedData, QIODevice::WriteOnly);
        stream << static_cast<quint8>(0); // 完整帧标记
        markedData.append(fullCompressedRaw);
        compressedData = markedData;
    }
    
    qint64 compressionTime = m_compressionTimer.elapsed();
    
    // 更新统计信息
    QByteArray originalData;
    QBuffer buffer(&originalData);
    buffer.open(QIODevice::WriteOnly);
    currentFrame.save(&buffer, "PNG");
    
    updateCompressionStats(originalData.size(), compressedData.size(), compressionTime, true);
    
    qCDebug(lcCompression, "Differential frame compressed: similarity=%.3f, size=%lld bytes in %lld ms",
            similarity, compressedData.size(), compressionTime);
    
    return compressedData;
}

QImage AdvancedCompressionManager::decompressFrame(const QByteArray &compressedData, const QImage &previousFrame)
{
    if (compressedData.isEmpty()) {
        qCWarning(lcCompression, "Attempted to decompress empty data");
        return QImage();
    }
    
    QDataStream stream(compressedData);
    quint8 frameType;
    stream >> frameType;
    
    if (stream.status() != QDataStream::Ok) {
        qCCritical(lcCompression, "Failed to read frame type from compressed data");
        return QImage();
    }
    
    // 读取实际压缩数据
    QByteArray actualData = compressedData.mid(sizeof(quint8));
    
    if (frameType == 0) {
        // 完整帧数据
        return Compression::decompressImageToImage(actualData);
    } else if (frameType == 1) {
        // 差分数据
        if (previousFrame.isNull()) {
            qCWarning(lcCompression, "Cannot apply differential decompression without previous frame");
            return QImage();
        }
        
        // 将前一帧转换为字节数组
        QByteArray previousData;
        QBuffer buffer(&previousData);
        buffer.open(QIODevice::WriteOnly);
        previousFrame.save(&buffer, "PNG");
        
        // 应用差分
        QByteArray reconstructedData = Compression::applyDifference(previousData, actualData);
        
        // 转换回图像
        QImage result;
        result.loadFromData(reconstructedData);
        return result;
    } else {
        qCCritical(lcCompression, "Unknown frame type: %d", frameType);
        return QImage();
    }
}

QList<AdvancedCompressionManager::ChangeRegion> AdvancedCompressionManager::detectChanges(const QImage &currentFrame, const QImage &previousFrame)
{
    if (currentFrame.isNull() || previousFrame.isNull()) {
        return QList<ChangeRegion>();
    }
    
    if (currentFrame.size() != previousFrame.size()) {
        qCWarning(lcCompression, "Frame size mismatch in change detection");
        return QList<ChangeRegion>();
    }
    
    switch (m_changeDetectionMode) {
    case PixelLevel:
        return detectChangesPixelLevel(currentFrame, previousFrame);
    case BlockLevel:
        return detectChangesBlockLevel(currentFrame, previousFrame);
    case RegionLevel:
        return detectChangesRegionLevel(currentFrame, previousFrame);
    case HybridLevel:
        return detectChangesHybridLevel(currentFrame, previousFrame);
    default:
        return detectChangesBlockLevel(currentFrame, previousFrame);
    }
}

double AdvancedCompressionManager::calculateFrameSimilarity(const QImage &frame1, const QImage &frame2)
{
    if (frame1.isNull() || frame2.isNull() || frame1.size() != frame2.size()) {
        return 0.0;
    }
    
    int width = frame1.width();
    int height = frame1.height();
    // qint64 totalPixels = static_cast<qint64>(width) * height; // 暂时注释掉未使用的变量
    qint64 similarPixels = 0;
    
    // 采样检测以提高性能
    int stepX = qMax(1, width / 100);
    int stepY = qMax(1, height / 100);
    qint64 sampledPixels = 0;
    
    for (int y = 0; y < height; y += stepY) {
        for (int x = 0; x < width; x += stepX) {
            QRgb pixel1 = frame1.pixel(x, y);
            QRgb pixel2 = frame2.pixel(x, y);
            
            // 计算颜色差异
            int rDiff = qAbs(qRed(pixel1) - qRed(pixel2));
            int gDiff = qAbs(qGreen(pixel1) - qGreen(pixel2));
            int bDiff = qAbs(qBlue(pixel1) - qBlue(pixel2));
            
            // 如果颜色差异小于阈值，认为是相似像素
            if (rDiff < 10 && gDiff < 10 && bDiff < 10) {
                similarPixels++;
            }
            sampledPixels++;
        }
    }
    
    return sampledPixels > 0 ? static_cast<double>(similarPixels) / sampledPixels : 0.0;
}

QByteArray AdvancedCompressionManager::compressChangedRegions(const QImage &currentFrame, const QImage &/*previousFrame*/, const QList<ChangeRegion> &changes)
{
    if (changes.isEmpty()) {
        return QByteArray();
    }
    
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    
    // 写入变化区域数量
    stream << static_cast<quint32>(changes.size());
    
    for (const ChangeRegion &change : changes) {
        // 写入区域信息
        stream << change.rect << change.changeIntensity;
        
        // 提取并压缩变化区域
        QImage regionImage = currentFrame.copy(change.rect);
        QByteArray regionData = Compression::compressImage(regionImage, 
                                                          Compression::JPEG, 
                                                          change.recommendedQuality);
        
        // 写入压缩后的区域数据
        stream << static_cast<quint32>(regionData.size());
        stream.writeRawData(regionData.data(), regionData.size());
    }
    
    return result;
}

Compression::Algorithm AdvancedCompressionManager::selectOptimalAlgorithm(const QByteArray &data, CompressionStrategy strategy)
{
    if (data.isEmpty()) {
        return Compression::ZLIB;
    }
    
    switch (strategy) {
    case FastStrategy:
        return Compression::LZ4; // LZ4 速度最快
        
    case HighCompressionStrategy:
        return Compression::ZSTD; // ZSTD 压缩率最高
        
    case BalancedStrategy:
        return Compression::ZLIB; // ZLIB 平衡性能
        
    case AdaptiveStrategy:
    default:
        return analyzeDataCharacteristics(data);
    }
}

Compression::Level AdvancedCompressionManager::selectOptimalLevel(const QByteArray &data, Compression::Algorithm /*algorithm*/)
{
    if (data.isEmpty()) {
        return Compression::DefaultCompression;
    }
    
    // 根据数据大小和算法选择压缩级别
    int dataSize = data.size();
    
    if (dataSize < 1024) {
        // 小数据，使用快速压缩
        return Compression::FastCompression;
    } else if (dataSize < 64 * 1024) {
        // 中等数据，使用默认压缩
        return Compression::DefaultCompression;
    } else {
        // 大数据，根据策略选择
        switch (m_strategy) {
        case FastStrategy:
            return Compression::FastCompression;
        case HighCompressionStrategy:
            return Compression::BestCompression;
        default:
            return Compression::DefaultCompression;
        }
    }
}

Compression::ImageFormat AdvancedCompressionManager::selectOptimalImageFormat(const QImage &image, CompressionStrategy strategy)
{
    if (image.isNull()) {
        return Compression::JPEG;
    }
    
    // 分析图像特征
    Compression::ImageAnalysis analysis = Compression::analyzeImage(image);
    
    switch (strategy) {
    case FastStrategy:
        return Compression::JPEG; // JPEG 编码速度快
        
    case HighCompressionStrategy:
        if (analysis.hasTransparency) {
            return Compression::PNG; // 需要透明度支持
        }
        return analysis.complexity > 0.5 ? Compression::JPEG : Compression::PNG;
        
    case BalancedStrategy:
    case AdaptiveStrategy:
    default:
        return Compression::selectOptimalFormat(image);
    }
}

int AdvancedCompressionManager::selectOptimalQuality(const QImage &image, Compression::ImageFormat format, CompressionStrategy strategy)
{
    if (format != Compression::JPEG) {
        return 95; // 非JPEG格式返回默认值
    }
    
    int baseQuality = Compression::selectOptimalQuality(image, format);
    
    // 根据策略调整质量
    switch (strategy) {
    case FastStrategy:
        return qMax(50, baseQuality - 15); // 降低质量以提高速度
        
    case HighCompressionStrategy:
        return qMin(95, baseQuality + 10); // 提高质量
        
    case BalancedStrategy:
    case AdaptiveStrategy:
    default:
        return baseQuality;
    }
}

AdvancedCompressionManager::CompressionStats AdvancedCompressionManager::getCompressionStats() const
{
    QMutexLocker locker(&m_statsMutex);
    return m_stats;
}

void AdvancedCompressionManager::resetStats()
{
    QMutexLocker locker(&m_statsMutex);
    m_stats = CompressionStats();
    m_stats.averageCompressionRatio = 0.0;
    m_stats.averageCompressionTime = 0;
    m_stats.changeDetectionAccuracy = 0.0;
    
    qCInfo(lcCompression, "Compression statistics reset");
}

double AdvancedCompressionManager::getCurrentCompressionRatio() const
{
    return m_lastCompressionRatio;
}

qint64 AdvancedCompressionManager::getCurrentCompressionTime() const
{
    return m_lastCompressionTime;
}

void AdvancedCompressionManager::setMaxFrameHistory(int maxFrames)
{
    QMutexLocker locker(&m_historyMutex);
    m_adaptiveConfig.maxFrameHistory = maxFrames;
    
    // 调整当前历史大小
    while (m_frameHistory.size() > static_cast<size_t>(maxFrames)) {
        m_frameHistory.pop_front();
    }
    
    qCInfo(lcCompression, "Max frame history set to: %d", maxFrames);
}

int AdvancedCompressionManager::maxFrameHistory() const
{
    return m_adaptiveConfig.maxFrameHistory;
}

void AdvancedCompressionManager::clearFrameHistory()
{
    QMutexLocker locker(&m_historyMutex);
    m_frameHistory.clear();
    qCInfo(lcCompression, "Frame history cleared");
}

QByteArray AdvancedCompressionManager::optimizeDifferentialData(const QByteArray &differentialData)
{
    if (differentialData.isEmpty()) {
        return QByteArray();
    }
    
    // 尝试进一步压缩差分数据
    QByteArray optimized = Compression::compress(differentialData, Compression::ZSTD, Compression::BestCompression);
    
    // 如果优化后的数据更大，返回原始数据
    return optimized.size() < differentialData.size() ? optimized : differentialData;
}

bool AdvancedCompressionManager::isFrameSignificantlyDifferent(const QImage &frame1, const QImage &frame2, double threshold)
{
    if (frame1.isNull() || frame2.isNull() || frame1.size() != frame2.size()) {
        return true;
    }
    
    // 快速采样检测
    int width = frame1.width();
    int height = frame1.height();
    int stepX = qMax(1, width / 50);
    int stepY = qMax(1, height / 50);
    
    int totalSamples = 0;
    int differentSamples = 0;
    
    for (int y = 0; y < height; y += stepY) {
        for (int x = 0; x < width; x += stepX) {
            QRgb pixel1 = frame1.pixel(x, y);
            QRgb pixel2 = frame2.pixel(x, y);
            
            int rDiff = qAbs(qRed(pixel1) - qRed(pixel2));
            int gDiff = qAbs(qGreen(pixel1) - qGreen(pixel2));
            int bDiff = qAbs(qBlue(pixel1) - qBlue(pixel2));
            
            if (rDiff > 15 || gDiff > 15 || bDiff > 15) {
                differentSamples++;
            }
            totalSamples++;
        }
    }
    
    double differenceRatio = totalSamples > 0 ? static_cast<double>(differentSamples) / totalSamples : 0.0;
    return differenceRatio > threshold;
}

QList<QRect> AdvancedCompressionManager::divideFrameIntoBlocks(const QSize &frameSize, int blockSize)
{
    QList<QRect> blocks;
    
    for (int y = 0; y < frameSize.height(); y += blockSize) {
        for (int x = 0; x < frameSize.width(); x += blockSize) {
            int width = qMin(blockSize, frameSize.width() - x);
            int height = qMin(blockSize, frameSize.height() - y);
            blocks.append(QRect(x, y, width, height));
        }
    }
    
    return blocks;
}

void AdvancedCompressionManager::updatePerformanceMetrics()
{
    QMutexLocker locker(&m_statsMutex);
    
    // 计算平均值
    if (m_stats.totalFramesProcessed > 0) {
        m_stats.averageCompressionRatio = (m_stats.totalBytesProcessed > 0) ? 
            static_cast<double>(m_stats.totalBytesCompressed) / m_stats.totalBytesProcessed : 0.0;
    }
    
    // 发出统计更新信号
    emit compressionStatsUpdated(m_stats);
    
    // 检查性能阈值
    if (m_stats.averageCompressionTime > 100) { // 100ms阈值
        emit performanceThresholdExceeded(QString("compression_time"), m_stats.averageCompressionTime);
    }
    
    if (m_stats.averageCompressionRatio < 0.3) { // 30%压缩率阈值
        emit performanceThresholdExceeded(QString("compression_ratio"), m_stats.averageCompressionRatio);
    }
}

void AdvancedCompressionManager::optimizeCompressionParameters()
{
    if (!m_adaptiveConfig.enableAdaptiveStrategy) {
        return;
    }
    
    adaptStrategyBasedOnPerformance();
    qCDebug(lcCompression, "Compression parameters optimized");
}

void AdvancedCompressionManager::onPerformanceTimer()
{
    updatePerformanceMetrics();
    optimizeCompressionParameters();
}

void AdvancedCompressionManager::updateCompressionStats(qint64 originalSize, qint64 compressedSize, qint64 compressionTime, bool isDifferential)
{
    QMutexLocker locker(&m_statsMutex);
    
    m_stats.totalBytesProcessed += originalSize;
    m_stats.totalBytesCompressed += compressedSize;
    m_stats.totalFramesProcessed++;
    
    if (isDifferential) {
        m_stats.differentialFrames++;
    } else {
        m_stats.fullFrames++;
    }
    
    // 更新平均压缩时间
    m_stats.averageCompressionTime = (m_stats.averageCompressionTime * (m_stats.totalFramesProcessed - 1) + compressionTime) / m_stats.totalFramesProcessed;
    
    // 更新当前值
    m_lastCompressionTime = compressionTime;
    m_lastCompressionRatio = originalSize > 0 ? static_cast<double>(compressedSize) / originalSize : 0.0;
}

QByteArray AdvancedCompressionManager::compressWithStrategy(const QByteArray &data, CompressionStrategy strategy)
{
    Compression::Algorithm algorithm = selectOptimalAlgorithm(data, strategy);
    Compression::Level level = selectOptimalLevel(data, algorithm);
    
    return Compression::compress(data, algorithm, level);
}

QList<AdvancedCompressionManager::ChangeRegion> AdvancedCompressionManager::detectChangesPixelLevel(const QImage &current, const QImage &previous)
{
    QList<ChangeRegion> changes;
    
    int width = current.width();
    int height = current.height();
    
    // 像素级检测（采样以提高性能）
    int stepX = qMax(1, width / 200);
    int stepY = qMax(1, height / 200);
    
    for (int y = 0; y < height; y += stepY) {
        for (int x = 0; x < width; x += stepX) {
            QRgb currentPixel = current.pixel(x, y);
            QRgb previousPixel = previous.pixel(x, y);
            
            if (currentPixel != previousPixel) {
                ChangeRegion region;
                region.rect = QRect(x, y, stepX, stepY);
                region.changeIntensity = 1.0; // 像素级检测强度固定为1.0
                region.bestAlgorithm = Compression::ZLIB;
                region.recommendedQuality = 85;
                changes.append(region);
            }
        }
    }
    
    return changes;
}

QList<AdvancedCompressionManager::ChangeRegion> AdvancedCompressionManager::detectChangesBlockLevel(const QImage &current, const QImage &previous)
{
    QList<ChangeRegion> changes;
    
    QList<QRect> blocks = divideFrameIntoBlocks(current.size(), m_adaptiveConfig.blockSize);
    
    for (const QRect &block : blocks) {
        double similarity = calculateBlockSimilarity(current, previous, block);
        
        if (similarity < (1.0 - m_adaptiveConfig.changeThreshold)) {
            ChangeRegion region;
            region.rect = block;
            region.changeIntensity = 1.0 - similarity;
            region.bestAlgorithm = Compression::ZLIB;
            region.recommendedQuality = qBound(50, static_cast<int>(85 * (1.0 - region.changeIntensity)), 95);
            changes.append(region);
        }
    }
    
    return changes;
}

QList<AdvancedCompressionManager::ChangeRegion> AdvancedCompressionManager::detectChangesRegionLevel(const QImage &current, const QImage &previous)
{
    // 区域级检测：将图像分为更大的区域进行检测
    QList<ChangeRegion> changes;
    
    int regionSize = m_adaptiveConfig.blockSize * 4; // 使用4倍块大小作为区域大小
    QList<QRect> regions = divideFrameIntoBlocks(current.size(), regionSize);
    
    for (const QRect &region : regions) {
        double similarity = calculateBlockSimilarity(current, previous, region);
        
        if (similarity < (1.0 - m_adaptiveConfig.changeThreshold)) {
            ChangeRegion changeRegion;
            changeRegion.rect = region;
            changeRegion.changeIntensity = 1.0 - similarity;
            changeRegion.bestAlgorithm = Compression::ZLIB;
            changeRegion.recommendedQuality = qBound(60, static_cast<int>(90 * (1.0 - changeRegion.changeIntensity)), 95);
            changes.append(changeRegion);
        }
    }
    
    return changes;
}

QList<AdvancedCompressionManager::ChangeRegion> AdvancedCompressionManager::detectChangesHybridLevel(const QImage &current, const QImage &previous)
{
    // 混合检测：结合块级和区域级检测
    QList<ChangeRegion> blockChanges = detectChangesBlockLevel(current, previous);
    QList<ChangeRegion> regionChanges = detectChangesRegionLevel(current, previous);
    
    // 合并结果，优先使用更精确的块级检测
    QList<ChangeRegion> hybridChanges = blockChanges;
    
    // 添加区域级检测中未被块级检测覆盖的区域
    for (const ChangeRegion &regionChange : regionChanges) {
        bool covered = false;
        for (const ChangeRegion &blockChange : blockChanges) {
            if (regionChange.rect.intersects(blockChange.rect)) {
                covered = true;
                break;
            }
        }
        
        if (!covered) {
            hybridChanges.append(regionChange);
        }
    }
    
    return hybridChanges;
}

double AdvancedCompressionManager::calculateBlockSimilarity(const QImage &image1, const QImage &image2, const QRect &block)
{
    if (block.isEmpty() || !image1.rect().contains(block) || !image2.rect().contains(block)) {
        return 0.0;
    }
    
    int totalPixels = block.width() * block.height();
    int similarPixels = 0;
    
    for (int y = block.top(); y <= block.bottom(); ++y) {
        for (int x = block.left(); x <= block.right(); ++x) {
            QRgb pixel1 = image1.pixel(x, y);
            QRgb pixel2 = image2.pixel(x, y);
            
            int rDiff = qAbs(qRed(pixel1) - qRed(pixel2));
            int gDiff = qAbs(qGreen(pixel1) - qGreen(pixel2));
            int bDiff = qAbs(qBlue(pixel1) - qBlue(pixel2));
            
            if (rDiff < 10 && gDiff < 10 && bDiff < 10) {
                similarPixels++;
            }
        }
    }
    
    return totalPixels > 0 ? static_cast<double>(similarPixels) / totalPixels : 0.0;
}

Compression::Algorithm AdvancedCompressionManager::analyzeDataCharacteristics(const QByteArray &data)
{
    if (data.isEmpty()) {
        return Compression::ZLIB;
    }
    
    // 分析数据特征
    int dataSize = data.size();
    
    // 计算数据的熵（简化版本）
    QHash<char, int> frequency;
    for (char byte : data) {
        frequency[byte]++;
    }
    
    double entropy = 0.0;
    for (auto it = frequency.begin(); it != frequency.end(); ++it) {
        double prob = static_cast<double>(it.value()) / dataSize;
        if (prob > 0) {
            entropy -= prob * std::log2(prob);
        }
    }
    
    // 根据熵值选择算法
    if (entropy < 4.0) {
        // 低熵数据，重复性高，适合高压缩率算法
        return Compression::ZSTD;
    } else if (entropy > 7.0) {
        // 高熵数据，随机性高，适合快速算法
        return Compression::LZ4;
    } else {
        // 中等熵数据，使用平衡算法
        return Compression::ZLIB;
    }
}

void AdvancedCompressionManager::adaptStrategyBasedOnPerformance()
{
    QMutexLocker locker(&m_statsMutex);
    
    // 根据性能指标自动调整策略
    if (m_stats.averageCompressionTime > 50) { // 压缩时间过长
        if (m_strategy != FastStrategy) {
            setCompressionStrategy(FastStrategy);
            qCInfo(lcCompression, "Auto-switched to FastStrategy due to high compression time");
        }
    } else if (m_stats.averageCompressionRatio > 0.8) { // 压缩率过低
        if (m_strategy != HighCompressionStrategy) {
            setCompressionStrategy(HighCompressionStrategy);
            qCInfo(lcCompression, "Auto-switched to HighCompressionStrategy due to low compression ratio");
        }
    } else {
        if (m_strategy != BalancedStrategy) {
            setCompressionStrategy(BalancedStrategy);
            qCInfo(lcCompression, "Auto-switched to BalancedStrategy for optimal performance");
        }
    }
}