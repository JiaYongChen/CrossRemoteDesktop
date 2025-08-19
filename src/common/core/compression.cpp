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
    
    // 简单的字节级差异压缩
    QByteArray diff;
    QDataStream stream(&diff, QIODevice::WriteOnly);
    
    int currentSize = current.size();
    int previousSize = previous.size();
    int maxSize = qMax(currentSize, previousSize);
    
    // 写入头部信息
    stream << currentSize;
    
    // 计算差异
    int unchangedBlocks = 0;
    const int blockSize = 64; // 64字节块
    
    for (int i = 0; i < maxSize; i += blockSize) {
        int currentBlockSize = qMin(blockSize, currentSize - i);
        int previousBlockSize = qMin(blockSize, previousSize - i);
        
        if (currentBlockSize <= 0) {
            // 当前数据已结束
            break;
        }
        
        bool blocksEqual = (previousBlockSize > 0 && currentBlockSize == previousBlockSize &&
                           memcmp(current.data() + i, previous.data() + i, currentBlockSize) == 0);
        
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
            stream.writeRawData(current.data() + i, currentBlockSize);
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
    
    // 如果差异数据比原数据还大，直接返回原数据
    if (diff.size() >= current.size()) {
        QByteArray fullData;
        QDataStream fullStream(&fullData, QIODevice::WriteOnly);
        fullStream << static_cast<qint32>(-1); // 标记为完整数据
        fullStream.writeRawData(current.data(), current.size());
        return fullData;
    }
    
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
    
    // 检查是否为完整数据
    if (targetSize == -1) {
        QByteArray result;
        result.resize(diff.size() - sizeof(qint32));
        stream.readRawData(result.data(), result.size());
        return result;
    }
    
    if (previous.isEmpty()) {
        return QByteArray();
    }
    
    QByteArray result;
    result.resize(targetSize);
    
    int pos = 0;
    const int blockSize = 64;
    
    while (!stream.atEnd() && pos < targetSize) {
        quint8 blockInfo;
        stream >> blockInfo;
        
        if (blockInfo == 0xFF) {
            // 跳过标记
            int skipBlocks;
            stream >> skipBlocks;
            
            // 跳过指定数量的块，从previous复制数据
            for (int i = 0; i < skipBlocks && pos < targetSize; i++) {
                int copyBytes = qMin(blockSize, targetSize - pos);
                if (copyBytes > 0 && pos + copyBytes <= previous.size()) {
                    memcpy(result.data() + pos, previous.data() + pos, copyBytes);
                }
                pos += copyBytes;
            }
        } else {
            // 数据块 - blockInfo表示要读取的字节数
            int bytesToRead = blockInfo;
            
            if (bytesToRead > 0 && pos + bytesToRead <= targetSize) {
                stream.readRawData(result.data() + pos, bytesToRead);
                pos += bytesToRead;
            }
        }
    }
    
    return result;
}