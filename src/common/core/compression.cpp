#include "compression.h"
#include "uiconstants.h"
#include "messageconstants.h"
#include <QDebug>
#include <zlib.h>
#include <lz4.h>
#include <lz4hc.h>
#include <zstd.h>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <cmath>

// 静态错误信息存储
thread_local QString Compression::s_lastError;

// Compression 静态方法实现
QByteArray Compression::compress(const QByteArray &data, Algorithm algorithm, Level level)
{
    switch (algorithm) {
    case ZLIB: {
        ZlibCompression zlib;
        zlib.setLevel(static_cast<int>(level));
        return zlib.compress(data);
    }
    case LZ4: {
        LZ4Compression lz4;
        lz4.setLevel(static_cast<int>(level));
        return lz4.compressWithHeader(data);
    }
    case ZSTD: {
        ZstdCompression zstd;
        zstd.setLevel(static_cast<int>(level));
        return zstd.compress(data);
    }
    default:
        qWarning() << MessageConstants::Compression::UNSUPPORTED_ALGORITHM;
        return QByteArray();
    }
}

QByteArray Compression::decompress(const QByteArray &compressedData, Algorithm algorithm)
{
    switch (algorithm) {
    case ZLIB: {
        ZlibCompression zlib;
        return zlib.decompress(compressedData);
    }
    case LZ4: {
        LZ4Compression lz4;
        return lz4.decompressWithHeader(compressedData);
    }
    case ZSTD: {
        ZstdCompression zstd;
        return zstd.decompress(compressedData);
    }
    default:
        qWarning() << MessageConstants::Compression::UNSUPPORTED_ALGORITHM;
        return QByteArray();
    }
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

// ZlibCompression 实现
ZlibCompression::ZlibCompression(QObject *parent)
    : QObject(parent)
    , m_level(UIConstants::DEFAULT_ZLIB_LEVEL)
    , m_windowBits(UIConstants::DEFAULT_ZLIB_WINDOW_BITS)
    , m_memLevel(UIConstants::DEFAULT_ZLIB_MEM_LEVEL)
    , m_strategy(Z_DEFAULT_STRATEGY)
{
}

ZlibCompression::~ZlibCompression()
{
}

void ZlibCompression::setLevel(int level)
{
    if (level >= Z_NO_COMPRESSION && level <= Z_BEST_COMPRESSION) {
        m_level = level;
    } else {
        qWarning() << MessageConstants::Compression::INVALID_COMPRESSION_LEVEL;
    }
}

int ZlibCompression::level() const
{
    return m_level;
}

void ZlibCompression::setWindowBits(int windowBits)
{
    if (windowBits >= UIConstants::MIN_WINDOW_BITS && windowBits <= UIConstants::MAX_WINDOW_BITS) {
        m_windowBits = windowBits;
    } else {
        qWarning() << MessageConstants::Compression::INVALID_WINDOW_BITS;
    }
}

int ZlibCompression::windowBits() const
{
    return m_windowBits;
}

void ZlibCompression::setMemLevel(int memLevel)
{
    if (memLevel >= 1 && memLevel <= 9) {
        m_memLevel = memLevel;
    } else {
        qWarning() << MessageConstants::Compression::INVALID_MEMORY_LEVEL;
    }
}

int ZlibCompression::memLevel() const
{
    return m_memLevel;
}

void ZlibCompression::setStrategy(int strategy)
{
    m_strategy = strategy;
}

int ZlibCompression::strategy() const
{
    return m_strategy;
}

QByteArray ZlibCompression::compress(const QByteArray &data)
{
    if (data.isEmpty()) {
        return QByteArray();
    }
    
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    
    if (deflateInit2(&stream, m_level, Z_DEFLATED, m_windowBits, m_memLevel, m_strategy) != Z_OK) {
        qWarning() << MessageConstants::Compression::ZLIB_INIT_FAILED;
        return QByteArray();
    }
    
    // 估算压缩后的大小
    uLong compressedSize = deflateBound(&stream, data.size());
    QByteArray compressed(compressedSize, 0);
    
    stream.avail_in = data.size();
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data()));
    stream.avail_out = compressed.size();
    stream.next_out = reinterpret_cast<Bytef*>(compressed.data());
    
    int result = deflate(&stream, Z_FINISH);
    
    if (result != Z_STREAM_END) {
        qWarning() << MessageConstants::Compression::ZLIB_COMPRESSION_FAILED << result;
        deflateEnd(&stream);
        return QByteArray();
    }
    
    compressed.resize(stream.total_out);
    deflateEnd(&stream);
    
    return compressed;
}

QByteArray ZlibCompression::decompress(const QByteArray &compressedData)
{
    if (compressedData.isEmpty()) {
        return QByteArray();
    }
    
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    
    if (inflateInit2(&stream, m_windowBits) != Z_OK) {
        qWarning() << MessageConstants::Compression::ZLIB_INIT_FAILED;
        return QByteArray();
    }
    
    // 初始解压缓冲区大小
    const int bufferSize = UIConstants::DECOMPRESSION_BUFFER_SIZE * 8; // 64KB
    QByteArray decompressed;
    QByteArray buffer(bufferSize, 0);
    
    stream.avail_in = compressedData.size();
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressedData.data()));
    
    int result;
    do {
        stream.avail_out = buffer.size();
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        
        result = inflate(&stream, Z_NO_FLUSH);
        
        if (result == Z_STREAM_ERROR || result == Z_DATA_ERROR || result == Z_MEM_ERROR) {
            qWarning() << MessageConstants::Compression::ZLIB_DECOMPRESSION_FAILED << result;
            inflateEnd(&stream);
            return QByteArray();
        }
        
        int decompressedBytes = buffer.size() - stream.avail_out;
        decompressed.append(buffer.data(), decompressedBytes);
        
    } while (result != Z_STREAM_END && stream.avail_out == 0);
    
    inflateEnd(&stream);
    
    if (result != Z_STREAM_END) {
        qWarning() << MessageConstants::Compression::ZLIB_DECOMPRESSION_INCOMPLETE;
        return QByteArray();
    }
    
    return decompressed;
}

double ZlibCompression::compressionRatio(const QByteArray &original, const QByteArray &compressed)
{
    if (original.isEmpty() || compressed.isEmpty()) {
        return 0.0;
    }
    
    return static_cast<double>(compressed.size()) / static_cast<double>(original.size());
}

// LZ4Compression 实现
LZ4Compression::LZ4Compression(QObject *parent)
    : QObject(parent)
    , m_level(UIConstants::DEFAULT_LZ4_LEVEL)
    , m_useHighCompression(false)
{
}

LZ4Compression::~LZ4Compression()
{
}

void LZ4Compression::setLevel(int level)
{
    if (level >= 1 && level <= LZ4HC_CLEVEL_MAX) {
        m_level = level;
    } else {
        qWarning() << MessageConstants::Compression::INVALID_LZ4_LEVEL << LZ4HC_CLEVEL_MAX;
    }
}

int LZ4Compression::level() const
{
    return m_level;
}

void LZ4Compression::setHighCompression(bool enabled)
{
    m_useHighCompression = enabled;
}

bool LZ4Compression::isHighCompression() const
{
    return m_useHighCompression;
}

QByteArray LZ4Compression::compress(const QByteArray &data)
{
    if (data.isEmpty()) {
        return QByteArray();
    }
    
    int maxCompressedSize = LZ4_compressBound(data.size());
    QByteArray compressed(maxCompressedSize, 0);
    
    int compressedSize;
    
    if (m_useHighCompression) {
        compressedSize = LZ4_compress_HC(data.data(), compressed.data(), data.size(), maxCompressedSize, m_level);
    } else {
        compressedSize = LZ4_compress_default(data.data(), compressed.data(), data.size(), maxCompressedSize);
    }
    
    if (compressedSize <= 0) {
        qWarning() << MessageConstants::Compression::LZ4_COMPRESSION_FAILED;
        return QByteArray();
    }
    
    compressed.resize(compressedSize);
    return compressed;
}

QByteArray LZ4Compression::decompress(const QByteArray &compressedData, int originalSize)
{
    if (compressedData.isEmpty() || originalSize <= 0) {
        qWarning() << MessageConstants::Compression::INVALID_INPUT;
        return QByteArray();
    }
    
    QByteArray decompressed(originalSize, 0);
    
    int decompressedSize = LZ4_decompress_safe(compressedData.data(), decompressed.data(),
                                              compressedData.size(), originalSize);
    
    if (decompressedSize < 0) {
        qWarning() << MessageConstants::Compression::LZ4_DECOMPRESSION_FAILED;
        return QByteArray();
    }
    
    if (decompressedSize != originalSize) {
        qWarning() << MessageConstants::Compression::SIZE_MISMATCH << originalSize << "Got:" << decompressedSize;
        return QByteArray();
    }
    
    return decompressed;
}

QByteArray LZ4Compression::compressWithHeader(const QByteArray &data)
{
    if (data.isEmpty()) {
        return QByteArray();
    }
    
    QByteArray compressed = compress(data);
    if (compressed.isEmpty()) {
        return QByteArray();
    }
    
    // 添加原始大小头部（4字节）
    QByteArray result;
    result.resize(sizeof(quint32) + compressed.size());
    
    // 写入原始大小
    quint32 originalSize = static_cast<quint32>(data.size());
    memcpy(result.data(), &originalSize, sizeof(quint32));
    
    // 写入压缩数据
    memcpy(result.data() + sizeof(quint32), compressed.data(), compressed.size());
    
    return result;
}

// ============== 缺失方法的实现 ==============

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
    CompressionInfo info;
    info.algorithm = algorithm;
    info.level = level;
    info.originalSize = data.size();
    info.success = false;
    
    QElapsedTimer timer;
    timer.start();
    
    QByteArray compressed = compress(data, algorithm, level);
    
    info.compressionTime = timer.elapsed();
    info.compressedSize = compressed.size();
    info.compressionRatio = (data.size() > 0) ? (double)compressed.size() / data.size() : 0.0;
    info.success = !compressed.isEmpty();
    
    return info;
}

QList<Compression::CompressionInfo> Compression::benchmarkAllAlgorithms(const QByteArray &data, Level level)
{
    QList<CompressionInfo> results;
    
    QList<Algorithm> algorithms = {ZLIB, LZ4, ZSTD};
    
    for (Algorithm alg : algorithms) {
        if (isAlgorithmSupported(alg)) {
            results.append(benchmarkCompression(data, alg, level));
        }
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
#ifdef LZ4_AVAILABLE
        return true;
#else
        return true; // LZ4库已包含
#endif
    case ZSTD:
#ifdef ZSTD_AVAILABLE
        return true;
#else
        return true; // ZSTD库已包含
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



QByteArray LZ4Compression::decompressWithHeader(const QByteArray &compressedData)
{
    if (compressedData.size() < static_cast<int>(sizeof(quint32))) {
        qWarning() << MessageConstants::Compression::INVALID_HEADER;
        return QByteArray();
    }
    
    // 读取原始大小
    quint32 originalSize;
    memcpy(&originalSize, compressedData.data(), sizeof(quint32));
    
    // 提取压缩数据
    QByteArray compressed = compressedData.mid(sizeof(quint32));
    
    return decompress(compressed, originalSize);
}

// ZstdCompression 实现
ZstdCompression::ZstdCompression(QObject *parent)
    : QObject(parent)
    , m_level(UIConstants::DEFAULT_ZSTD_LEVEL)
{
}

ZstdCompression::~ZstdCompression()
{
}

void ZstdCompression::setLevel(int level)
{
    int minLevel = ZSTD_minCLevel();
    int maxLevel = ZSTD_maxCLevel();
    
    if (level >= minLevel && level <= maxLevel) {
        m_level = level;
    } else {
        qWarning() << MessageConstants::Compression::INVALID_ZSTD_LEVEL << minLevel << "-" << maxLevel;
    }
}

int ZstdCompression::level() const
{
    return m_level;
}

QByteArray ZstdCompression::compress(const QByteArray &data)
{
    if (data.isEmpty()) {
        return QByteArray();
    }
    
    size_t maxCompressedSize = ZSTD_compressBound(data.size());
    QByteArray compressed(maxCompressedSize, 0);
    
    size_t compressedSize = ZSTD_compress(compressed.data(), maxCompressedSize,
                                         data.data(), data.size(), m_level);
    
    if (ZSTD_isError(compressedSize)) {
        qWarning() << MessageConstants::Compression::ZSTD_COMPRESSION_FAILED << ZSTD_getErrorName(compressedSize);
        return QByteArray();
    }
    
    compressed.resize(compressedSize);
    return compressed;
}

QByteArray ZstdCompression::decompress(const QByteArray &compressedData)
{
    if (compressedData.isEmpty()) {
        return QByteArray();
    }
    
    // 获取解压后的大小
    unsigned long long decompressedSize = ZSTD_getFrameContentSize(compressedData.data(), compressedData.size());
    
    if (decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
        qWarning() << MessageConstants::Compression::INVALID_COMPRESSED_DATA;
        return QByteArray();
    }
    
    if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        qWarning() << MessageConstants::Compression::UNKNOWN_SIZE;
        return QByteArray();
    }
    
    QByteArray decompressed(decompressedSize, 0);
    
    size_t actualSize = ZSTD_decompress(decompressed.data(), decompressedSize,
                                       compressedData.data(), compressedData.size());
    
    if (ZSTD_isError(actualSize)) {
        qWarning() << MessageConstants::Compression::ZSTD_DECOMPRESSION_FAILED << ZSTD_getErrorName(actualSize);
        return QByteArray();
    }
    
    if (actualSize != decompressedSize) {
        qWarning() << MessageConstants::Compression::SIZE_MISMATCH;
        return QByteArray();
    }
    
    return decompressed;
}

QByteArray ZstdCompression::compressStream(const QByteArray &data)
{
    if (data.isEmpty()) {
        return QByteArray();
    }
    
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    if (!cctx) {
        qWarning() << MessageConstants::Compression::CONTEXT_CREATION_FAILED;
        return QByteArray();
    }
    
    size_t maxCompressedSize = ZSTD_compressBound(data.size());
    QByteArray compressed(maxCompressedSize, 0);
    
    ZSTD_inBuffer input = { data.data(), static_cast<size_t>(data.size()), 0 };
    ZSTD_outBuffer output = { compressed.data(), static_cast<size_t>(compressed.size()), 0 };
    
    // 设置压缩级别
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, m_level);
    
    size_t result = ZSTD_compressStream2(cctx, &output, &input, ZSTD_e_end);
    
    ZSTD_freeCCtx(cctx);
    
    if (ZSTD_isError(result)) {
        qWarning() << MessageConstants::Compression::ZSTD_COMPRESSION_FAILED << ZSTD_getErrorName(result);
        return QByteArray();
    }
    
    compressed.resize(output.pos);
    return compressed;
}

QByteArray ZstdCompression::decompressStream(const QByteArray &compressedData)
{
    if (compressedData.isEmpty()) {
        return QByteArray();
    }
    
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    if (!dctx) {
        qWarning() << MessageConstants::Compression::CONTEXT_CREATION_FAILED;
        return QByteArray();
    }
    
    // 初始缓冲区大小
    const size_t bufferSize = UIConstants::DECOMPRESSION_BUFFER_SIZE * 8; // 64KB
    QByteArray decompressed;
    QByteArray buffer(bufferSize, 0);
    
    ZSTD_inBuffer input = { compressedData.data(), static_cast<size_t>(compressedData.size()), 0 };
    
    while (input.pos < input.size) {
        ZSTD_outBuffer output = { buffer.data(), bufferSize, 0 };
        
        size_t result = ZSTD_decompressStream(dctx, &output, &input);
        
        if (ZSTD_isError(result)) {
            qWarning() << MessageConstants::Compression::ZSTD_DECOMPRESSION_FAILED << ZSTD_getErrorName(result);
            ZSTD_freeDCtx(dctx);
            return QByteArray();
        }
        
        decompressed.append(buffer.data(), output.pos);
        
        if (result == 0) {
            break; // 解压完成
        }
    }
    
    ZSTD_freeDCtx(dctx);
    return decompressed;
}

// CompressionUtils 实现
CompressionUtils::Algorithm CompressionUtils::detectAlgorithm(const QByteArray &data)
{
    if (data.isEmpty()) {
        return Algorithm::Unknown;
    }
    
    // 检查Zlib/Deflate魔数
    if (data.size() >= 2) {
        unsigned char b1 = static_cast<unsigned char>(data[0]);
        unsigned char b2 = static_cast<unsigned char>(data[1]);
        
        // Zlib魔数检查
        if ((b1 & 0x0F) == 0x08 && (b1 & 0xF0) <= 0x70 && (b1 * 256 + b2) % 31 == 0) {
            return Algorithm::Zlib;
        }
    }
    
    // 检查Zstd魔数
    if (data.size() >= 4) {
        const unsigned char zstdMagic[] = { 0x28, 0xB5, 0x2F, 0xFD };
        if (memcmp(data.data(), zstdMagic, 4) == 0) {
            return Algorithm::Zstd;
        }
    }
    
    // LZ4没有标准魔数，难以检测
    return Algorithm::Unknown;
}

QByteArray CompressionUtils::compress(const QByteArray &data, Algorithm algorithm, int level)
{
    switch (algorithm) {
    case Algorithm::Zlib: {
        ZlibCompression zlib;
        zlib.setLevel(level);
        return zlib.compress(data);
    }
    case Algorithm::LZ4: {
        LZ4Compression lz4;
        lz4.setLevel(level);
        return lz4.compressWithHeader(data);
    }
    case Algorithm::Zstd: {
        ZstdCompression zstd;
        zstd.setLevel(level);
        return zstd.compress(data);
    }
    default:
        qWarning() << MessageConstants::Compression::UNSUPPORTED_ALGORITHM;
        return QByteArray();
    }
}

QByteArray CompressionUtils::decompress(const QByteArray &compressedData, Algorithm algorithm)
{
    switch (algorithm) {
    case Algorithm::Zlib: {
        ZlibCompression zlib;
        return zlib.decompress(compressedData);
    }
    case Algorithm::LZ4: {
        LZ4Compression lz4;
        return lz4.decompressWithHeader(compressedData);
    }
    case Algorithm::Zstd: {
        ZstdCompression zstd;
        return zstd.decompress(compressedData);
    }
    default:
        qWarning() << "Unsupported compression algorithm";
        return QByteArray();
    }
}

QByteArray CompressionUtils::autoCompress(const QByteArray &data, int level)
{
    if (data.isEmpty()) {
        return QByteArray();
    }
    
    // 对于小数据，使用LZ4（速度快）
    if (data.size() < UIConstants::SMALL_DATA_THRESHOLD) {
        return compress(data, Algorithm::LZ4, level);
    }
    
    // 对于中等大小数据，使用Zstd（平衡压缩率和速度）
    if (data.size() < UIConstants::COMPRESSION_THRESHOLD) {
        return compress(data, Algorithm::Zstd, level);
    }
    
    // 对于大数据，使用Zlib（更好的压缩率）
    return compress(data, Algorithm::Zlib, level);
}

QByteArray CompressionUtils::autoDecompress(const QByteArray &compressedData)
{
    Algorithm algorithm = detectAlgorithm(compressedData);
    if (algorithm == Algorithm::Unknown) {
        qWarning() << MessageConstants::Compression::ALGORITHM_DETECTION_FAILED;
        return QByteArray();
    }
    
    return decompress(compressedData, algorithm);
}

double CompressionUtils::calculateCompressionRatio(const QByteArray &original, const QByteArray &compressed)
{
    if (original.isEmpty() || compressed.isEmpty()) {
        return 0.0;
    }
    
    return static_cast<double>(compressed.size()) / static_cast<double>(original.size());
}

CompressionUtils::CompressionInfo CompressionUtils::benchmark(const QByteArray &data, Algorithm algorithm, int level)
{
    CompressionInfo info;
    info.algorithm = algorithm;
    info.level = level;
    info.originalSize = data.size();
    
    QElapsedTimer timer;
    
    // 压缩基准测试
    timer.start();
    QByteArray compressed = compress(data, algorithm, level);
    info.compressionTime = timer.elapsed();
    
    if (compressed.isEmpty()) {
        info.success = false;
        return info;
    }
    
    info.compressedSize = compressed.size();
    info.compressionRatio = calculateCompressionRatio(data, compressed);
    
    // 解压基准测试
    timer.restart();
    QByteArray decompressed = decompress(compressed, algorithm);
    info.decompressionTime = timer.elapsed();
    
    info.success = !decompressed.isEmpty() && decompressed == data;
    
    return info;
}

QList<CompressionUtils::CompressionInfo> CompressionUtils::benchmarkAll(const QByteArray &data, int level)
{
    QList<CompressionInfo> results;
    
    QList<Algorithm> algorithms = { Algorithm::Zlib, Algorithm::LZ4, Algorithm::Zstd };
    
    for (Algorithm algorithm : algorithms) {
        CompressionInfo info = benchmark(data, algorithm, level);
        results.append(info);
    }
    
    return results;
}

CompressionUtils::Algorithm CompressionUtils::findBestAlgorithm(const QByteArray &data, OptimizationTarget target, int level)
{
    QList<CompressionInfo> results = benchmarkAll(data, level);
    
    if (results.isEmpty()) {
        return Algorithm::Unknown;
    }
    
    CompressionInfo best = results.first();
    
    for (const CompressionInfo &info : results) {
        if (!info.success) {
            continue;
        }
        
        switch (target) {
        case OptimizationTarget::CompressionRatio:
            if (info.compressionRatio < best.compressionRatio) {
                best = info;
            }
            break;
        case OptimizationTarget::CompressionSpeed:
            if (info.compressionTime < best.compressionTime) {
                best = info;
            }
            break;
        case OptimizationTarget::DecompressionSpeed:
            if (info.decompressionTime < best.decompressionTime) {
                best = info;
            }
            break;
        case OptimizationTarget::SPEED:
            if (info.compressionTime < best.compressionTime) {
                best = info;
            }
            break;
        case OptimizationTarget::RATIO:
            if (info.compressionRatio < best.compressionRatio) {
                best = info;
            }
            break;
        case OptimizationTarget::BALANCED:
        case OptimizationTarget::Balanced:
            // 综合评分：压缩率权重0.4，压缩速度权重0.3，解压速度权重0.3
            double currentScore = info.compressionRatio * 0.4 + 
                                (info.compressionTime / 1000.0) * 0.3 + 
                                (info.decompressionTime / 1000.0) * 0.3;
            double bestScore = best.compressionRatio * 0.4 + 
                             (best.compressionTime / 1000.0) * 0.3 + 
                             (best.decompressionTime / 1000.0) * 0.3;
            if (currentScore < bestScore) {
                best = info;
            }
            break;
        }
    }
    
    return best.algorithm;
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