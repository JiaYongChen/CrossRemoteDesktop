#include "compression.h"
#include "constants.h"
#include "messageconstants.h"
#include <QtCore/QDebug>
#include "logging_categories.h"
#include <zlib.h>
#include <QtCore/QElapsedTimer>
#include <QtCore/QBuffer>
#include <QtCore/QIODevice>
#include <QtGui/QImageWriter>
#include <QtCore/QMessageLogger>
#include <QtCore/QCryptographicHash>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <cmath>
#include <cstring>

// New pluggable compressors
#include "common/codec/compressor_factory.h"

// 静态错误信息存储
thread_local QString Compression::s_lastError;

// Compression 静态方法实现
QByteArray Compression::compress(const QByteArray &data, Algorithm algorithm, Level level)
{
    // Route through pluggable compressors
    auto comp = CompressorFactory::create(algorithm);
    if (!comp) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcCompression) << MessageConstants::Compression::UNSUPPORTED_ALGORITHM;
        return QByteArray();
    }
    return comp->compress(data, static_cast<int>(level));
}

QByteArray Compression::decompress(const QByteArray &compressedData, Algorithm algorithm)
{
    auto comp = CompressorFactory::create(algorithm);
    if (!comp) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcCompression) << MessageConstants::Compression::UNSUPPORTED_ALGORITHM;
        return QByteArray();
    }
    return comp->decompress(compressedData);
}

QByteArray Compression::compressString(const QString &text, Algorithm algorithm, Level level)
{
    return compress(text.toUtf8(), algorithm, level);
}

QString Compression::decompressString(const QByteArray &compressedData, Algorithm algorithm)
{
    return QString::fromUtf8(decompress(compressedData, algorithm));
}

QByteArray Compression::compressImage(const QPixmap &pixmap, ImageFormat format, int quality)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    
    QString formatStr;
    switch (format) {
    case JPEG: formatStr = "JPEG"; break;
    case PNG: formatStr = "PNG"; break;
    case WEBP: formatStr = "WEBP"; break;
    case BMP: formatStr = "BMP"; break;
    case TIFF: formatStr = "TIFF"; break;
    }
    
    pixmap.save(&buffer, formatStr.toLocal8Bit().data(), quality);
    return data;
}

QByteArray Compression::compressImage(const QImage &image, ImageFormat format, int quality)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    
    QString formatStr;
    switch (format) {
    case JPEG: formatStr = "JPEG"; break;
    case PNG: formatStr = "PNG"; break;
    case WEBP: formatStr = "WEBP"; break;
    case BMP: formatStr = "BMP"; break;
    case TIFF: formatStr = "TIFF"; break;
    }
    
    image.save(&buffer, formatStr.toLocal8Bit().data(), quality);
    return data;
}

QPixmap Compression::decompressImageToPixmap(const QByteArray &compressedData)
{
    QPixmap pixmap;
    pixmap.loadFromData(compressedData);
    return pixmap;
}

QImage Compression::decompressImageToImage(const QByteArray &compressedData)
{
    QImage image;
    image.loadFromData(compressedData);
    return image;
}

// 区域压缩实现
QByteArray Compression::compressRegion(const QPixmap &pixmap, const QRect &region, ImageFormat format, int quality)
{
    if (pixmap.isNull() || region.isEmpty()) {
        s_lastError = "Invalid pixmap or region";
        return QByteArray();
    }
    
    // 提取指定区域
    QPixmap regionPixmap = pixmap.copy(region);
    return compressImage(regionPixmap, format, quality);
}

QByteArray Compression::compressRegion(const QImage &image, const QRect &region, ImageFormat format, int quality)
{
    if (image.isNull() || region.isEmpty()) {
        s_lastError = "Invalid image or region";
        return QByteArray();
    }
    
    // 提取指定区域
    QImage regionImage = image.copy(region);
    return compressImage(regionImage, format, quality);
}

// 自适应压缩实现
QByteArray Compression::adaptiveCompress(const QByteArray &data, Level level)
{
    if (data.isEmpty()) {
        s_lastError = "Empty data";
        return QByteArray();
    }
    
    // 根据数据大小和内容选择最佳算法
    Algorithm bestAlgorithm = selectBestAlgorithm(data, level);
    return compress(data, bestAlgorithm, level);
}

QByteArray Compression::adaptiveDecompress(const QByteArray &compressedData)
{
    if (compressedData.isEmpty()) {
        s_lastError = "Empty compressed data";
        return QByteArray();
    }
    
    // 尝试检测压缩算法
    if (compressedData.size() < 4) {
        s_lastError = "Invalid compressed data format";
        return QByteArray();
    }
    
    // 简单的算法检测（基于魔数）
    const unsigned char* data = reinterpret_cast<const unsigned char*>(compressedData.constData());
    
    // ZLIB魔数检测
    if ((data[0] == 0x78 && (data[1] == 0x01 || data[1] == 0x9C || data[1] == 0xDA))) {
        return decompress(compressedData, ZLIB);
    }
    
    // ZSTD魔数检测
    if (data[0] == 0x28 && data[1] == 0xB5 && data[2] == 0x2F && data[3] == 0xFD) {
        return decompress(compressedData, ZSTD);
    }
    
    // 默认尝试ZLIB
    QByteArray result = decompress(compressedData, ZLIB);
    if (!result.isEmpty()) {
        return result;
    }
    
    // 尝试其他算法
    result = decompress(compressedData, LZ4);
    if (!result.isEmpty()) {
        return result;
    }
    
    result = decompress(compressedData, ZSTD);
    if (!result.isEmpty()) {
        return result;
    }
    
    s_lastError = "Unable to detect compression algorithm";
    return QByteArray();
}

// 压缩信息获取
Compression::CompressionInfo Compression::getCompressionInfo(const QByteArray &original, const QByteArray &compressed, Algorithm algorithm)
{
    CompressionInfo info;
    info.algorithm = algorithm;
    info.level = DefaultCompression;
    info.originalSize = original.size();
    info.compressedSize = compressed.size();
    info.compressionRatio = (original.size() > 0) ? (double)compressed.size() / original.size() : 0.0;
    info.compressionTime = 0;
    info.success = !compressed.isEmpty();
    
    return info;
}

// 压缩性能测试
Compression::CompressionInfo Compression::benchmarkCompression(const QByteArray &data, Algorithm algorithm, Level level)
{
    CompressionInfo info{};
    info.algorithm = algorithm;
    info.level = level;
    info.originalSize = data.size();
    info.success = false;

    auto comp = CompressorFactory::create(algorithm);
    if (!comp) { return info; }

    QElapsedTimer timer;
    timer.start();
    const QByteArray compressed = comp->compress(data, static_cast<int>(level));
    info.compressionTime = timer.elapsed();
    info.compressedSize = compressed.size();
    info.compressionRatio = (data.size() > 0 && !compressed.isEmpty()) ? (double)compressed.size() / data.size() : 0.0;
    if (compressed.isEmpty()) { return info; }

    // Verify roundtrip
    const QByteArray decompressed = comp->decompress(compressed);
    info.success = (!decompressed.isEmpty() && decompressed == data);
    return info;
}

QList<Compression::CompressionInfo> Compression::benchmarkAllAlgorithms(const QByteArray &data, Level level)
{
    QList<CompressionInfo> results;
    QList<Algorithm> algorithms;
    algorithms.append(ZLIB);
#ifdef HAVE_LZ4
    algorithms.append(LZ4);
#endif
#ifdef HAVE_ZSTD
    algorithms.append(ZSTD);
#endif
    for (Algorithm alg : algorithms) {
        results.append(benchmarkCompression(data, alg, level));
    }
    return results;
}

// 最佳算法选择
Compression::Algorithm Compression::selectBestAlgorithm(const QByteArray &data, Level level)
{
    Q_UNUSED(level)
    if (data.isEmpty()) {
        return ZLIB;
    }
    
    // 对于小数据，使用LZ4（速度快）
    if (data.size() < 1024) {
        return LZ4;
    }
    
    // 对于中等数据，使用ZLIB（平衡）
    if (data.size() < 1024 * 1024) {
        return ZLIB;
    }
    
    // 对于大数据，使用ZSTD（压缩率高）
    return ZSTD;
}

Compression::Level Compression::selectBestLevel(const QByteArray &data, Algorithm algorithm)
{
    Q_UNUSED(algorithm)
    
    if (data.isEmpty()) {
        return DefaultCompression;
    }
    
    // 根据数据大小选择压缩级别
    if (data.size() < 1024) {
        return FastCompression;
    } else if (data.size() < 1024 * 1024) {
        return DefaultCompression;
    } else {
        return BestCompression;
    }
}

// 压缩验证
bool Compression::verifyCompression(const QByteArray &original, const QByteArray &compressed, Algorithm algorithm)
{
    if (original.isEmpty() || compressed.isEmpty()) {
        return false;
    }
    
    QByteArray decompressed = decompress(compressed, algorithm);
    return decompressed == original;
}

// 图像质量评估
double Compression::calculatePSNR(const QImage &original, const QImage &compressed)
{
    if (original.isNull() || compressed.isNull() || 
        original.size() != compressed.size()) {
        s_lastError = "Invalid images for PSNR calculation";
        return 0.0;
    }
    
    double mse = 0.0;
    int width = original.width();
    int height = original.height();
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            QRgb orig = original.pixel(x, y);
            QRgb comp = compressed.pixel(x, y);
            
            int rDiff = qRed(orig) - qRed(comp);
            int gDiff = qGreen(orig) - qGreen(comp);
            int bDiff = qBlue(orig) - qBlue(comp);
            
            mse += rDiff * rDiff + gDiff * gDiff + bDiff * bDiff;
        }
    }
    
    mse /= (width * height * 3);
    
    if (mse == 0.0) {
        return 100.0; // 完全相同
    }
    
    double psnr = 10.0 * log10((255.0 * 255.0) / mse);
    return psnr;
}

double Compression::calculateSSIM(const QImage &original, const QImage &compressed)
{
    if (original.isNull() || compressed.isNull() || 
        original.size() != compressed.size()) {
        s_lastError = "Invalid images for SSIM calculation";
        return 0.0;
    }
    
    // 简化的SSIM计算（仅计算亮度）
    int width = original.width();
    int height = original.height();
    
    double mean1 = 0.0, mean2 = 0.0;
    
    // 计算均值
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            QRgb orig = original.pixel(x, y);
            QRgb comp = compressed.pixel(x, y);
            
            mean1 += qGray(orig);
            mean2 += qGray(comp);
        }
    }
    
    int totalPixels = width * height;
    mean1 /= totalPixels;
    mean2 /= totalPixels;
    
    double var1 = 0.0, var2 = 0.0, covar = 0.0;
    
    // 计算方差和协方差
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            QRgb orig = original.pixel(x, y);
            QRgb comp = compressed.pixel(x, y);
            
            double gray1 = qGray(orig) - mean1;
            double gray2 = qGray(comp) - mean2;
            
            var1 += gray1 * gray1;
            var2 += gray2 * gray2;
            covar += gray1 * gray2;
        }
    }
    
    var1 /= totalPixels;
    var2 /= totalPixels;
    covar /= totalPixels;
    
    // SSIM常数
    double c1 = 6.5025; // (0.01 * 255)^2
    double c2 = 58.5225; // (0.03 * 255)^2
    
    double ssim = ((2 * mean1 * mean2 + c1) * (2 * covar + c2)) /
                  ((mean1 * mean1 + mean2 * mean2 + c1) * (var1 + var2 + c2));
    
    return ssim;
}

// 工具函数
QString Compression::algorithmToString(Algorithm algorithm)
{
    switch (algorithm) {
    case ZLIB: return "ZLIB";
    case GZIP: return "GZIP";
    case DEFLATE: return "DEFLATE";
    case LZ4: return "LZ4";
    case ZSTD: return "ZSTD";
    case BZIP2: return "BZIP2";
    default: return "UNKNOWN";
    }
}

Compression::Algorithm Compression::stringToAlgorithm(const QString &algorithmName)
{
    QString name = algorithmName.toUpper();
    if (name == "ZLIB") return ZLIB;
    if (name == "GZIP") return GZIP;
    if (name == "DEFLATE") return DEFLATE;
    if (name == "LZ4") return LZ4;
    if (name == "ZSTD") return ZSTD;
    if (name == "BZIP2") return BZIP2;
    return ZLIB; // 默认
}

QString Compression::imageFormatToString(ImageFormat format)
{
    switch (format) {
    case JPEG: return "JPEG";
    case PNG: return "PNG";
    case WEBP: return "WEBP";
    case BMP: return "BMP";
    case TIFF: return "TIFF";
    default: return "UNKNOWN";
    }
}

Compression::ImageFormat Compression::stringToImageFormat(const QString &formatName)
{
    QString name = formatName.toUpper();
    if (name == "JPEG" || name == "JPG") return JPEG;
    if (name == "PNG") return PNG;
    if (name == "WEBP") return WEBP;
    if (name == "BMP") return BMP;
    if (name == "TIFF" || name == "TIF") return TIFF;
    return JPEG; // 默认
}

// 支持的算法检查
bool Compression::isAlgorithmSupported(Algorithm algorithm)
{
    switch (algorithm) {
    case ZLIB:
        return true;
    case LZ4:
#ifdef HAVE_LZ4
    return true;
#else
    return false;
#endif
    case ZSTD:
#ifdef HAVE_ZSTD
    return true;
#else
    return false;
#endif
    case GZIP:
    case DEFLATE:
        return true; // 可以通过zlib实现
    case BZIP2:
#ifdef BZIP2_AVAILABLE
        return true;
#else
        return false;
#endif
    default:
        return false;
    }
}

QList<Compression::Algorithm> Compression::supportedAlgorithms()
{
    QList<Algorithm> algorithms;
    
    if (isAlgorithmSupported(ZLIB)) algorithms.append(ZLIB);
    if (isAlgorithmSupported(GZIP)) algorithms.append(GZIP);
    if (isAlgorithmSupported(DEFLATE)) algorithms.append(DEFLATE);
    if (isAlgorithmSupported(LZ4)) algorithms.append(LZ4);
    if (isAlgorithmSupported(ZSTD)) algorithms.append(ZSTD);
    if (isAlgorithmSupported(BZIP2)) algorithms.append(BZIP2);
    
    return algorithms;
}

bool Compression::isImageFormatSupported(ImageFormat format)
{
    switch (format) {
    case JPEG:
    case PNG:
    case BMP:
        return true;
    case WEBP:
    case TIFF:
        // 检查Qt是否支持这些格式
        return QImageWriter::supportedImageFormats().contains(imageFormatToString(format).toLatin1());
    default:
        return false;
    }
}

QList<Compression::ImageFormat> Compression::supportedImageFormats()
{
    QList<ImageFormat> formats;
    
    if (isImageFormatSupported(JPEG)) formats.append(JPEG);
    if (isImageFormatSupported(PNG)) formats.append(PNG);
    if (isImageFormatSupported(WEBP)) formats.append(WEBP);
    if (isImageFormatSupported(BMP)) formats.append(BMP);
    if (isImageFormatSupported(TIFF)) formats.append(TIFF);
    
    return formats;
}

// 错误处理
QString Compression::lastError()
{
    return s_lastError;
}

// ============== 图像格式检测实现 ==============

// 检测图像数据的格式
Compression::ImageFormat Compression::detectImageFormat(const QByteArray &imageData)
{
    if (imageData.isEmpty()) {
        return JPEG; // 默认格式
    }
    
    // 检查各种图像格式的魔数
    if (isJpegData(imageData)) {
        return JPEG;
    }
    if (isPngData(imageData)) {
        return PNG;
    }
    if (isBmpData(imageData)) {
        return BMP;
    }
    if (isWebpData(imageData)) {
        return WEBP;
    }
    if (isTiffData(imageData)) {
        return TIFF;
    }
    
    // 默认返回JPEG格式
    return JPEG;
}

// 检测是否为JPEG格式数据
bool Compression::isJpegData(const QByteArray &data)
{
    if (data.size() < 4) {
        return false;
    }
    
    // JPEG文件魔数: FF D8 FF
    const unsigned char *bytes = reinterpret_cast<const unsigned char*>(data.constData());
    return (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF);
}

// 检测是否为PNG格式数据
bool Compression::isPngData(const QByteArray &data)
{
    if (data.size() < 8) {
        return false;
    }
    
    // PNG文件魔数: 89 50 4E 47 0D 0A 1A 0A
    const unsigned char *bytes = reinterpret_cast<const unsigned char*>(data.constData());
    return (bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 &&
            bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A);
}

// 检测是否为BMP格式数据
bool Compression::isBmpData(const QByteArray &data)
{
    if (data.size() < 2) {
        return false;
    }
    
    // BMP文件魔数: 42 4D ("BM")
    const unsigned char *bytes = reinterpret_cast<const unsigned char*>(data.constData());
    return (bytes[0] == 0x42 && bytes[1] == 0x4D);
}

// 检测是否为WebP格式数据
bool Compression::isWebpData(const QByteArray &data)
{
    if (data.size() < 12) {
        return false;
    }
    
    // WebP文件魔数: "RIFF" + 4字节文件大小 + "WEBP"
    const char *bytes = data.constData();
    return (memcmp(bytes, "RIFF", 4) == 0 && memcmp(bytes + 8, "WEBP", 4) == 0);
}

// 检测是否为TIFF格式数据
bool Compression::isTiffData(const QByteArray &data)
{
    if (data.size() < 4) {
        return false;
    }
    
    // TIFF文件魔数: "II*\0" (小端) 或 "MM\0*" (大端)
    const unsigned char *bytes = reinterpret_cast<const unsigned char*>(data.constData());
    return ((bytes[0] == 0x49 && bytes[1] == 0x49 && bytes[2] == 0x2A && bytes[3] == 0x00) ||
            (bytes[0] == 0x4D && bytes[1] == 0x4D && bytes[2] == 0x00 && bytes[3] == 0x2A));
}

// 获取图像格式名称
QString Compression::getImageFormatName(const QByteArray &imageData)
{
    ImageFormat format = detectImageFormat(imageData);
    return imageFormatToString(format);
}

// ============== 自适应压缩策略实现 ==============

// 分析图像特征
Compression::ImageAnalysis Compression::analyzeImage(const QImage &image)
{
    ImageAnalysis analysis;
    analysis.imageSize = image.size();
    analysis.hasTransparency = image.hasAlphaChannel();
    
    if (image.isNull() || image.width() == 0 || image.height() == 0) {
        analysis.complexity = 0.0;
        analysis.colorVariance = 0.0;
        analysis.uniqueColors = 0;
        return analysis;
    }
    
    // 计算图像复杂度和颜色统计
    QSet<QRgb> uniqueColors;
    int pixelCount = 0;
    
    // 采样分析以提高性能（对于大图像）
    int stepX = qMax(1, image.width() / 100);  // 最多采样100x100像素
    int stepY = qMax(1, image.height() / 100);
    
    QVector<int> redValues, greenValues, blueValues;
    redValues.reserve(10000);
    greenValues.reserve(10000);
    blueValues.reserve(10000);
    
    for (int y = 0; y < image.height(); y += stepY) {
        for (int x = 0; x < image.width(); x += stepX) {
            QRgb pixel = image.pixel(x, y);
            uniqueColors.insert(pixel);
            
            redValues.append(qRed(pixel));
            greenValues.append(qGreen(pixel));
            blueValues.append(qBlue(pixel));
            pixelCount++;
        }
    }
    
    analysis.uniqueColors = uniqueColors.size();
    
    // 计算颜色方差
    if (pixelCount > 0) {
        auto calculateVariance = [](const QVector<int> &values) -> double {
            if (values.isEmpty()) return 0.0;
            
            double mean = 0.0;
            for (int value : values) {
                mean += value;
            }
            mean /= values.size();
            
            double variance = 0.0;
            for (int value : values) {
                variance += (value - mean) * (value - mean);
            }
            return variance / values.size();
        };
        
        double redVar = calculateVariance(redValues);
        double greenVar = calculateVariance(greenValues);
        double blueVar = calculateVariance(blueValues);
        
        analysis.colorVariance = (redVar + greenVar + blueVar) / 3.0;
    }
    
    // 计算复杂度（基于颜色数量和方差）
    double colorComplexity = qMin(1.0, analysis.uniqueColors / 1000.0);
    double varianceComplexity = qMin(1.0, analysis.colorVariance / 10000.0);
    analysis.complexity = (colorComplexity + varianceComplexity) / 2.0;
    
    return analysis;
}

// 选择最优图像格式
Compression::ImageFormat Compression::selectOptimalFormat(const QImage &image)
{
    ImageAnalysis analysis = analyzeImage(image);
    
    // 如果有透明度，优先选择PNG
    if (analysis.hasTransparency) {
        return PNG;
    }
    
    // 根据图像特征选择格式
    if (analysis.uniqueColors < 256) {
        // 颜色较少，PNG更适合
        return PNG;
    }
    
    if (analysis.complexity < 0.3) {
        // 简单图像，PNG压缩效果更好
        return PNG;
    }
    
    // 复杂图像或照片，JPEG更适合
    return JPEG;
}

// 选择最优压缩质量
int Compression::selectOptimalQuality(const QImage &image, ImageFormat format)
{
    if (format != JPEG) {
        return 95; // 非JPEG格式返回默认值
    }
    
    ImageAnalysis analysis = analyzeImage(image);
    
    // 根据图像尺寸调整质量
    int baseQuality = 85;
    
    // 大图像可以使用稍低的质量
    int totalPixels = analysis.imageSize.width() * analysis.imageSize.height();
    if (totalPixels > 1920 * 1080) {
        baseQuality = 80; // 高分辨率图像
    } else if (totalPixels < 640 * 480) {
        baseQuality = 90; // 小图像保持高质量
    }
    
    // 根据复杂度调整
    if (analysis.complexity > 0.7) {
        baseQuality += 5; // 复杂图像需要更高质量
    } else if (analysis.complexity < 0.3) {
        baseQuality -= 5; // 简单图像可以降低质量
    }
    
    return qBound(50, baseQuality, 95);
}

// 自适应图像压缩
QByteArray Compression::adaptiveCompressImage(const QImage &image)
{
    if (image.isNull()) {
        return QByteArray();
    }
    
    // 选择最优格式和质量
    ImageFormat optimalFormat = selectOptimalFormat(image);
    int optimalQuality = selectOptimalQuality(image, optimalFormat);
    
    // 执行压缩
    return compressImage(image, optimalFormat, optimalQuality);
}

// ============== 数据完整性验证实现 ==============

// CRC32校验表
static const quint32 crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

// 计算CRC32校验值
quint32 Compression::calculateCRC32(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFF;
    
    for (int i = 0; i < data.size(); ++i) {
        quint8 byte = static_cast<quint8>(data[i]);
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

// 计算MD5校验值
QByteArray Compression::calculateMD5(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

// 验证数据完整性（CRC32）
bool Compression::validateDataIntegrity(const QByteArray &data, quint32 expectedCRC)
{
    quint32 actualCRC = calculateCRC32(data);
    return actualCRC == expectedCRC;
}

// 验证数据完整性（MD5）
bool Compression::validateDataIntegrity(const QByteArray &data, const QByteArray &expectedMD5)
{
    QByteArray actualMD5 = calculateMD5(data);
    return actualMD5 == expectedMD5;
}

// 添加数据校验和
QByteArray Compression::addDataChecksum(const QByteArray &data)
{
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    
    // 写入数据长度
    stream << static_cast<quint32>(data.size());
    
    // 写入CRC32校验值
    quint32 crc = calculateCRC32(data);
    stream << crc;
    
    // 写入原始数据
    result.append(data);
    
    return result;
}

// 提取并验证数据
QPair<QByteArray, bool> Compression::extractAndValidateData(const QByteArray &dataWithChecksum)
{
    QPair<QByteArray, bool> result;
    result.second = false; // 默认验证失败
    
    if (dataWithChecksum.size() < 8) { // 至少需要4字节长度 + 4字节CRC
        return result;
    }
    
    QDataStream stream(dataWithChecksum);
    
    // 读取数据长度
    quint32 dataSize;
    stream >> dataSize;
    
    // 读取CRC32校验值
    quint32 expectedCRC;
    stream >> expectedCRC;
    
    // 检查数据长度是否合理
    if (dataSize > static_cast<quint32>(dataWithChecksum.size() - 8)) {
        return result;
    }
    
    // 提取原始数据
    QByteArray originalData = dataWithChecksum.mid(8, dataSize);
    
    // 验证数据完整性
    result.second = validateDataIntegrity(originalData, expectedCRC);
    result.first = originalData;
    
    return result;
}

// ============== 增强的回退机制实现 ==============

// 鲁棒的差分应用函数
Compression::FallbackResult Compression::robustApplyDifference(const QByteArray &previous, const QByteArray &difference)
{
    FallbackResult result;
    result.usedFallback = false;
    result.attemptCount = 0;
    
    if (difference.isEmpty()) {
        result.data = previous;
        result.errorMessage = "Empty difference data";
        return result;
    }
    
    // 第一次尝试：正常差分应用
    result.attemptCount++;
    QByteArray reconstructed = applyBinaryDiff(previous, difference);
    
    if (!reconstructed.isEmpty() && isValidImageData(reconstructed)) {
        result.data = reconstructed;
        return result; // 成功，无需回退
    }
    
    // 第二次尝试：将差分数据当作完整数据处理
    result.attemptCount++;
    result.usedFallback = true;
    
    if (isValidImageData(difference)) {
        result.data = difference;
        result.errorMessage = "Used difference data as complete frame";
        return result;
    }
    
    // 第三次尝试：数据修复
    result.attemptCount++;
    QByteArray repairedData = repairCorruptedData(difference);
    
    if (!repairedData.isEmpty() && isValidImageData(repairedData)) {
        result.data = repairedData;
        result.errorMessage = "Used repaired data";
        return result;
    }
    
    // 第四次尝试：使用前一帧数据
    result.attemptCount++;
    if (!previous.isEmpty() && isValidImageData(previous)) {
        result.data = previous;
        result.errorMessage = "Fallback to previous frame";
        return result;
    }
    
    // 所有尝试都失败
    result.data = QByteArray();
    result.errorMessage = "All fallback attempts failed";
    return result;
}

// 验证图像数据有效性
bool Compression::isValidImageData(const QByteArray &data)
{
    if (data.isEmpty() || data.size() < 10) {
        return false;
    }
    
    // 检查常见图像格式的魔数
    const char *dataPtr = data.constData();
    
    // JPEG魔数检查
    if (data.size() >= 3 && 
        static_cast<unsigned char>(dataPtr[0]) == 0xFF && 
        static_cast<unsigned char>(dataPtr[1]) == 0xD8 && 
        static_cast<unsigned char>(dataPtr[2]) == 0xFF) {
        return true;
    }
    
    // PNG魔数检查
    if (data.size() >= 8 && 
        static_cast<unsigned char>(dataPtr[0]) == 0x89 && 
        dataPtr[1] == 'P' && dataPtr[2] == 'N' && dataPtr[3] == 'G' &&
        static_cast<unsigned char>(dataPtr[4]) == 0x0D && 
        static_cast<unsigned char>(dataPtr[5]) == 0x0A && 
        static_cast<unsigned char>(dataPtr[6]) == 0x1A && 
        static_cast<unsigned char>(dataPtr[7]) == 0x0A) {
        return true;
    }
    
    // BMP魔数检查
    if (data.size() >= 2 && dataPtr[0] == 'B' && dataPtr[1] == 'M') {
        return true;
    }
    
    // WebP魔数检查
    if (data.size() >= 12 && 
        dataPtr[0] == 'R' && dataPtr[1] == 'I' && dataPtr[2] == 'F' && dataPtr[3] == 'F' &&
        dataPtr[8] == 'W' && dataPtr[9] == 'E' && dataPtr[10] == 'B' && dataPtr[11] == 'P') {
        return true;
    }
    
    return false;
}

// 修复损坏的数据
QByteArray Compression::repairCorruptedData(const QByteArray &data)
{
    if (data.isEmpty()) {
        return QByteArray();
    }
    
    QByteArray repairedData = data;
    
    // 尝试修复JPEG数据
    if (data.size() >= 3) {
        const char *dataPtr = data.constData();
        
        // 检查是否可能是损坏的JPEG
        if (static_cast<unsigned char>(dataPtr[0]) == 0xFF) {
            // 修复JPEG SOI标记
            if (static_cast<unsigned char>(dataPtr[1]) != 0xD8) {
                repairedData[1] = static_cast<char>(0xD8);
                if (data.size() >= 3 && static_cast<unsigned char>(dataPtr[2]) != 0xFF) {
                    repairedData[2] = static_cast<char>(0xFF);
                }
            }
            
            // 确保JPEG以EOI标记结束
            if (repairedData.size() >= 2) {
                int lastIndex = repairedData.size() - 1;
                int secondLastIndex = repairedData.size() - 2;
                
                if (static_cast<unsigned char>(repairedData[secondLastIndex]) != 0xFF ||
                    static_cast<unsigned char>(repairedData[lastIndex]) != 0xD9) {
                    repairedData[secondLastIndex] = static_cast<char>(0xFF);
                    repairedData[lastIndex] = static_cast<char>(0xD9);
                }
            }
        }
    }
    
    return repairedData;
}

// 带回退机制的数据处理
Compression::FallbackResult Compression::processWithFallback(const QByteArray &data, const QByteArray &previousFrame)
{
    FallbackResult result;
    result.usedFallback = false;
    result.attemptCount = 0;
    
    // 第一次尝试：直接使用数据
    result.attemptCount++;
    if (isValidImageData(data)) {
        result.data = data;
        return result;
    }
    
    // 第二次尝试：应用差分
    if (!previousFrame.isEmpty()) {
        result.attemptCount++;
        FallbackResult diffResult = robustApplyDifference(previousFrame, data);
        if (!diffResult.data.isEmpty()) {
            result.data = diffResult.data;
            result.usedFallback = diffResult.usedFallback;
            result.errorMessage = diffResult.errorMessage;
            result.attemptCount += diffResult.attemptCount;
            return result;
        }
    }
    
    // 第三次尝试：数据修复
    result.attemptCount++;
    result.usedFallback = true;
    QByteArray repairedData = repairCorruptedData(data);
    
    if (isValidImageData(repairedData)) {
        result.data = repairedData;
        result.errorMessage = "Used repaired data";
        return result;
    }
    
    // 最后尝试：使用前一帧
    if (!previousFrame.isEmpty() && isValidImageData(previousFrame)) {
        result.attemptCount++;
        result.data = previousFrame;
        result.errorMessage = "Fallback to previous frame";
        return result;
    }
    
    // 所有尝试都失败
    result.data = QByteArray();
    result.errorMessage = "All processing attempts failed";
    return result;
}

// ============== StreamCompressor 实现 ==============

Compression::StreamCompressor::StreamCompressor(Algorithm algorithm, Level level)
    : m_algorithm(algorithm), m_level(level), m_stream(nullptr), m_initialized(false)
{
}

Compression::StreamCompressor::~StreamCompressor()
{
    if (m_initialized && m_stream) {
        switch (m_algorithm) {
        case ZLIB:
            deflateEnd(static_cast<z_stream*>(m_stream));
            break;
        default:
            break;
        }
        delete static_cast<z_stream*>(m_stream);
    }
}

bool Compression::StreamCompressor::initialize()
{
    if (m_initialized) {
        return true;
    }
    
    switch (m_algorithm) {
    case ZLIB: {
        m_stream = new z_stream;
        z_stream* zs = static_cast<z_stream*>(m_stream);
        memset(zs, 0, sizeof(z_stream));
        
        int ret = deflateInit(zs, static_cast<int>(m_level));
        if (ret != Z_OK) {
            delete zs;
            m_stream = nullptr;
            return false;
        }
        break;
    }
    default:
        return false;
    }
    
    m_initialized = true;
    return true;
}

QByteArray Compression::StreamCompressor::compress(const QByteArray &data, bool finish)
{
    if (!m_initialized && !initialize()) {
        return QByteArray();
    }
    
    switch (m_algorithm) {
    case ZLIB: {
        z_stream* zs = static_cast<z_stream*>(m_stream);
        
        QByteArray output;
        output.resize(data.size() + 1024); // 预分配空间
        
        zs->next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
        zs->avail_in = data.size();
        zs->next_out = reinterpret_cast<Bytef*>(output.data());
        zs->avail_out = output.size();
        
        int ret = deflate(zs, finish ? Z_FINISH : Z_NO_FLUSH);
        if (ret < 0) {
            return QByteArray();
        }
        
        int outputSize = output.size() - zs->avail_out;
        output.resize(outputSize);
        return output;
    }
    default:
        return QByteArray();
    }
}

void Compression::StreamCompressor::reset()
{
    if (m_initialized && m_stream) {
        switch (m_algorithm) {
        case ZLIB:
            deflateReset(static_cast<z_stream*>(m_stream));
            break;
        default:
            break;
        }
    }
}

// ============== StreamDecompressor 实现 ==============

Compression::StreamDecompressor::StreamDecompressor(Algorithm algorithm)
    : m_algorithm(algorithm), m_stream(nullptr), m_initialized(false)
{
}

Compression::StreamDecompressor::~StreamDecompressor()
{
    if (m_initialized && m_stream) {
        switch (m_algorithm) {
        case ZLIB:
            inflateEnd(static_cast<z_stream*>(m_stream));
            break;
        default:
            break;
        }
        delete static_cast<z_stream*>(m_stream);
    }
}

bool Compression::StreamDecompressor::initialize()
{
    if (m_initialized) {
        return true;
    }
    
    switch (m_algorithm) {
    case ZLIB: {
        m_stream = new z_stream;
        z_stream* zs = static_cast<z_stream*>(m_stream);
        memset(zs, 0, sizeof(z_stream));
        
        int ret = inflateInit(zs);
        if (ret != Z_OK) {
            delete zs;
            m_stream = nullptr;
            return false;
        }
        break;
    }
    default:
        return false;
    }
    
    m_initialized = true;
    return true;
}

QByteArray Compression::StreamDecompressor::decompress(const QByteArray &data)
{
    if (!m_initialized && !initialize()) {
        return QByteArray();
    }
    
    switch (m_algorithm) {
    case ZLIB: {
        z_stream* zs = static_cast<z_stream*>(m_stream);
        
        QByteArray output;
        output.resize(data.size() * 4); // 预分配空间
        
        zs->next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
        zs->avail_in = data.size();
        zs->next_out = reinterpret_cast<Bytef*>(output.data());
        zs->avail_out = output.size();
        
        int ret = inflate(zs, Z_NO_FLUSH);
        if (ret < 0) {
            return QByteArray();
        }
        
        int outputSize = output.size() - zs->avail_out;
        output.resize(outputSize);
        return output;
    }
    default:
        return QByteArray();
    }
}

void Compression::StreamDecompressor::reset()
{
    if (m_initialized && m_stream) {
        switch (m_algorithm) {
        case ZLIB:
            inflateReset(static_cast<z_stream*>(m_stream));
            break;
        default:
            break;
        }
    }
}

// 差异压缩实现
QByteArray Compression::compressDifference(const QByteArray &current, const QByteArray &previous)
{
    if (previous.isEmpty()) {
        // 如果没有前一帧，直接返回当前数据
        return current;
    }
    
    return calculateBinaryDiff(current, previous);
}

QByteArray Compression::applyDifference(const QByteArray &previous, const QByteArray &difference)
{
    if (previous.isEmpty()) {
        // 如果没有前一帧，差异数据就是完整数据
        return difference;
    }
    
    return applyBinaryDiff(previous, difference);
}

// 二进制差异计算实现
QByteArray Compression::calculateBinaryDiff(const QByteArray &current, const QByteArray &previous)
{
    if (previous.isEmpty() || current.isEmpty()) {
        return current;
    }
    
    // 优化的字节级差异压缩算法
    QByteArray diff;
    diff.reserve(qMin(current.size() / 2, 64 * 1024)); // 预分配内存，避免频繁重分配
    QDataStream stream(&diff, QIODevice::WriteOnly);
    
    int currentSize = current.size();
    int previousSize = previous.size();
    
    // 写入头部信息
    stream << currentSize;
    
    // 固定使用64字节块大小以保持与applyBinaryDiff的一致性
    const int blockSize = 64;
    
    int unchangedBlocks = 0;
    const char *currentData = current.constData();
    const char *previousData = previous.constData();
    
    // 使用更高效的比较方式
    for (int i = 0; i < currentSize; i += blockSize) {
        int currentBlockSize = qMin(blockSize, currentSize - i);
        int previousBlockSize = qMin(blockSize, previousSize - i);
        
        bool blocksEqual = false;
        if (i < previousSize && currentBlockSize == previousBlockSize) {
            // 使用memcmp进行快速比较
            blocksEqual = (memcmp(currentData + i, previousData + i, currentBlockSize) == 0);
        }
        
        if (!blocksEqual) {
            // 块不同，写入差异标记和数据
            if (unchangedBlocks > 0) {
                // 先写入跳过的块数
                stream << static_cast<quint8>(0xFF); // 跳过标记
                stream << unchangedBlocks;
                unchangedBlocks = 0;
            }
            
            // 写入变化的块
            stream << static_cast<quint8>(currentBlockSize);
            stream.writeRawData(currentData + i, currentBlockSize);
        } else {
            // 块相同，计数
            unchangedBlocks++;
        }
    }
    
    // 如果最后还有未写入的跳过块
    if (unchangedBlocks > 0) {
        stream << static_cast<quint8>(0xFF);
        stream << unchangedBlocks;
    }
    
    // 压缩效率检查：如果差异数据比原数据大太多，返回完整数据
    if (diff.size() >= current.size() * 0.9) { // 90%阈值
        QByteArray fullData;
        fullData.reserve(current.size() + 4);
        QDataStream fullStream(&fullData, QIODevice::WriteOnly);
        fullStream << static_cast<qint32>(-1); // 标记为完整数据
        fullStream.writeRawData(current.data(), current.size());
        return fullData;
    }
    
    // 收缩内存以节省空间
    diff.squeeze();
    return diff;
}

// 应用二进制差异实现
QByteArray Compression::applyBinaryDiff(const QByteArray &previous, const QByteArray &diff)
{
    if (diff.isEmpty()) {
        return previous;
    }
    
    QDataStream stream(diff);
    qint32 targetSize;
    stream >> targetSize;
    
    // 检查流状态
    if (stream.status() != QDataStream::Ok) {
        qCWarning(lcCompression, "applyBinaryDiff: Failed to read target size, stream error");
        return QByteArray();
    }
    
    // 检查是否为完整数据
    if (targetSize == -1) {
        QByteArray result;
        result.resize(diff.size() - sizeof(qint32));
        stream.readRawData(result.data(), result.size());
        return result;
    }
    
    // 验证 targetSize 的合理性
    if (targetSize < 0) {
        qCWarning(lcCompression, "applyBinaryDiff: Invalid target size: %d (negative value)", targetSize);
        return QByteArray();
    }
    
    if (targetSize > 100 * 1024 * 1024) { // 限制为100MB
        qCWarning(lcCompression, "applyBinaryDiff: Target size too large: %d bytes", targetSize);
        return QByteArray();
    }
    
    if (previous.isEmpty()) {
        qCWarning(lcCompression, "applyBinaryDiff: Previous data is empty but target size is %d", targetSize);
        return QByteArray();
    }
    
    QByteArray result;
    result.resize(targetSize);
    
    int pos = 0;
    const int blockSize = 64;
    
    int iterationCount = 0;
    const int maxIterations = qMax(1000, targetSize / blockSize * 2); // 更合理的最大迭代次数
    int lastPos = -1; // 用于检测位置是否有进展
    int stuckCount = 0; // 连续未进展的次数
    
    while (!stream.atEnd() && pos < targetSize && iterationCount < maxIterations) {
        iterationCount++;
        
        // 检测是否卡住（位置没有进展）
        if (pos == lastPos) {
            stuckCount++;
            if (stuckCount > 10) {
                qCWarning(lcCompression, "applyBinaryDiff: Position stuck at %d for %d iterations, aborting", pos, stuckCount);
                return QByteArray(); // 返回空数组，触发回退到原始数据
            }
        } else {
            stuckCount = 0;
            lastPos = pos;
        }
        
        quint8 blockInfo;
        stream >> blockInfo;
        
        // 检查流状态
        if (stream.status() != QDataStream::Ok) {
            qCWarning(lcCompression, "Stream error in applyBinaryDiff, status: %d", stream.status());
            return QByteArray(); // 立即返回空数组
        }
        
        if (blockInfo == 0xFF) {
            // 跳过标记
            int skipBlocks;
            stream >> skipBlocks;
            
            // 更严格的skipBlocks检查
            if (stream.status() != QDataStream::Ok || skipBlocks < 0 || skipBlocks > (targetSize / blockSize + 10)) {
                qCWarning(lcCompression, "Invalid skipBlocks value: %d or stream error", skipBlocks);
                return QByteArray();
            }
            
            // 检查是否会导致位置超出范围
            int expectedNewPos = pos + skipBlocks * blockSize;
            if (expectedNewPos > targetSize) {
                qCWarning(lcCompression, "skipBlocks would exceed target size: %d > %d", expectedNewPos, targetSize);
                return QByteArray();
            }
            
            // 跳过指定数量的块，从previous复制数据
            for (int i = 0; i < skipBlocks && pos < targetSize; i++) {
                int copyBytes = qMin(blockSize, targetSize - pos);
                if (copyBytes > 0 && pos + copyBytes <= previous.size()) {
                    memcpy(result.data() + pos, previous.data() + pos, copyBytes);
                } else if (copyBytes > 0) {
                    // 如果previous数据不足，用零填充
                    memset(result.data() + pos, 0, copyBytes);
                }
                pos += copyBytes;
            }
        } else {
            // 数据块 - blockInfo表示要读取的字节数
            int bytesToRead = blockInfo;
            
            // 更严格的bytesToRead检查
            if (bytesToRead < 0 || bytesToRead > blockSize || pos + bytesToRead > targetSize) {
                qCWarning(lcCompression, "Invalid bytesToRead value: %d pos: %d targetSize: %d", bytesToRead, pos, targetSize);
                return QByteArray();
            }
            
            if (bytesToRead > 0) {
                int actualRead = stream.readRawData(result.data() + pos, bytesToRead);
                if (actualRead != bytesToRead) {
                    qCWarning(lcCompression, "Failed to read expected bytes: %d actual: %d", bytesToRead, actualRead);
                    return QByteArray();
                }
                pos += bytesToRead;
            }
        }
    }
    
    if (iterationCount >= maxIterations) {
        qCWarning(lcCompression, "applyBinaryDiff: Maximum iterations reached, possible infinite loop detected");
        return QByteArray(); // 返回空数组而不是部分结果
    }
    
    // 验证结果的完整性
    if (pos != targetSize) {
        qCWarning(lcCompression, "applyBinaryDiff: Incomplete result, expected size: %d actual: %d", targetSize, pos);
        return QByteArray();
    }
    
    return result;
}