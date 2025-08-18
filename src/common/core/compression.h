#ifndef COMPRESSION_H
#define COMPRESSION_H

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QRect>
#include <functional>
#include <QtGui/QPixmap>
#include <QtGui/QImage>
#include "constants.h"

class Compression
{
public:
    // 压缩算法类型
    enum Algorithm {
        ZLIB,
        GZIP,
        DEFLATE,
        LZ4,
        ZSTD,
        BZIP2
    };
    
    // 压缩级别
    enum Level {
        NoCompression = 0,
        FastestCompression = 1,
        FastCompression = 3,
        DefaultCompression = 6,
        BestCompression = 9
    };
    
    // 图像压缩格式
    enum ImageFormat {
        JPEG,
        PNG,
        WEBP,
        BMP,
        TIFF
    };
    
    // 通用数据压缩
    static QByteArray compress(const QByteArray &data, Algorithm algorithm = ZLIB, Level level = DefaultCompression);
    static QByteArray decompress(const QByteArray &compressedData, Algorithm algorithm = ZLIB);
    
    // 字符串压缩
    static QByteArray compressString(const QString &text, Algorithm algorithm = ZLIB, Level level = DefaultCompression);
    static QString decompressString(const QByteArray &compressedData, Algorithm algorithm = ZLIB);
    
    // 图像压缩
    static QByteArray compressImage(const QPixmap &pixmap, ImageFormat format = JPEG, int quality = 85);
    static QByteArray compressImage(const QImage &image, ImageFormat format = JPEG, int quality = 85);
    static QPixmap decompressImageToPixmap(const QByteArray &compressedData);
    static QImage decompressImageToImage(const QByteArray &compressedData);
    
    // 差分压缩（用于屏幕更新）
    static QByteArray compressDifference(const QByteArray &current, const QByteArray &previous);
    static QByteArray applyDifference(const QByteArray &previous, const QByteArray &difference);
    
    // 区域压缩（压缩图像的特定区域）
    static QByteArray compressRegion(const QPixmap &pixmap, const QRect &region, ImageFormat format = JPEG, int quality = 85);
    static QByteArray compressRegion(const QImage &image, const QRect &region, ImageFormat format = JPEG, int quality = 85);
    
    // 自适应压缩（根据内容自动选择最佳算法）
    static QByteArray adaptiveCompress(const QByteArray &data, Level level = DefaultCompression);
    static QByteArray adaptiveDecompress(const QByteArray &compressedData);
    
    // 流式压缩（用于大文件）
    class StreamCompressor {
    public:
        explicit StreamCompressor(Algorithm algorithm = ZLIB, Level level = DefaultCompression);
        ~StreamCompressor();
        
        bool initialize();
        QByteArray compress(const QByteArray &data, bool finish = false);
        void reset();
        
    private:
        Algorithm m_algorithm;
        Level m_level;
        void *m_stream;
        bool m_initialized;
    };
    
    class StreamDecompressor {
    public:
        explicit StreamDecompressor(Algorithm algorithm = ZLIB);
        ~StreamDecompressor();
        
        bool initialize();
        QByteArray decompress(const QByteArray &data);
        void reset();
        
    private:
        Algorithm m_algorithm;
        void *m_stream;
        bool m_initialized;
    };
    
    // 压缩信息
    struct CompressionInfo {
        Algorithm algorithm;
        Level level;
        qint64 originalSize;
        qint64 compressedSize;
        double compressionRatio;
        qint64 compressionTime; // 毫秒
        bool success;
    };
    
    static CompressionInfo getCompressionInfo(const QByteArray &original, const QByteArray &compressed, Algorithm algorithm);
    
    // 压缩性能测试
    static CompressionInfo benchmarkCompression(const QByteArray &data, Algorithm algorithm, Level level);
    static QList<CompressionInfo> benchmarkAllAlgorithms(const QByteArray &data, Level level = DefaultCompression);
    
    // 最佳算法选择
    static Algorithm selectBestAlgorithm(const QByteArray &data, Level level = DefaultCompression);
    static Level selectBestLevel(const QByteArray &data, Algorithm algorithm = ZLIB);
    
    // 压缩验证
    static bool verifyCompression(const QByteArray &original, const QByteArray &compressed, Algorithm algorithm);
    
    // 图像质量评估
    static double calculatePSNR(const QImage &original, const QImage &compressed);
    static double calculateSSIM(const QImage &original, const QImage &compressed);
    
    // 工具函数
    static QString algorithmToString(Algorithm algorithm);
    static Algorithm stringToAlgorithm(const QString &algorithmName);
    static QString imageFormatToString(ImageFormat format);
    static ImageFormat stringToImageFormat(const QString &formatName);
    
    // 支持的算法检查
    static bool isAlgorithmSupported(Algorithm algorithm);
    static QList<Algorithm> supportedAlgorithms();
    static bool isImageFormatSupported(ImageFormat format);
    static QList<ImageFormat> supportedImageFormats();
    
    // 错误处理
    static QString lastError();
    
private:
    // ZLIB压缩实现
    static QByteArray compressZlib(const QByteArray &data, Level level);
    static QByteArray decompressZlib(const QByteArray &compressedData);
    
    // GZIP压缩实现
    static QByteArray compressGzip(const QByteArray &data, Level level);
    static QByteArray decompressGzip(const QByteArray &compressedData);
    
    // BZIP2压缩实现（如果可用）
#ifdef BZIP2_AVAILABLE
    static QByteArray compressBzip2(const QByteArray &data, Level level);
    static QByteArray decompressBzip2(const QByteArray &compressedData);
#endif
    
    // 图像处理辅助函数
    static QByteArray pixmapToByteArray(const QPixmap &pixmap, ImageFormat format, int quality);
    static QByteArray imageToByteArray(const QImage &image, ImageFormat format, int quality);
    static const char* imageFormatToMimeType(ImageFormat format);
    
    // 差分算法实现
    static QByteArray calculateBinaryDiff(const QByteArray &current, const QByteArray &previous);
    static QByteArray applyBinaryDiff(const QByteArray &previous, const QByteArray &diff);
    
    // 性能测量
    static qint64 measureCompressionTime(std::function<QByteArray()> compressionFunc);
    
    // 错误信息
    static thread_local QString s_lastError;
    
    // 禁止实例化
    Compression() = delete;
    ~Compression() = delete;
    Compression(const Compression&) = delete;
    Compression& operator=(const Compression&) = delete;
};

// Zlib压缩类
class ZlibCompression : public QObject
{
    Q_OBJECT
    
public:
    explicit ZlibCompression(QObject *parent = nullptr);
    ~ZlibCompression();
    
    void setLevel(int level);
    int level() const;
    
    void setWindowBits(int windowBits);
    int windowBits() const;
    
    void setMemLevel(int memLevel);
    int memLevel() const;
    
    void setStrategy(int strategy);
    int strategy() const;
    
    QByteArray compress(const QByteArray &data);
    QByteArray decompress(const QByteArray &compressedData);
    
    static double compressionRatio(const QByteArray &original, const QByteArray &compressed);
    
private:
    int m_level;
    int m_windowBits;
    int m_memLevel;
    int m_strategy;
};

// 压缩工具类
class CompressionUtils
{
public:
    enum Algorithm {
        Unknown,
        Zlib,
        ZLIB,
        LZ4,
        Zstd,
        ZSTD,
        AUTO
    };
    
    enum OptimizationTarget {
        SPEED,
        RATIO,
        BALANCED,
        CompressionRatio,
        CompressionSpeed,
        DecompressionSpeed,
        Balanced
    };
    
    struct CompressionInfo {
        Algorithm algorithm;
        int level;
        qint64 originalSize;
        qint64 compressedSize;
        double compressionRatio;
        qint64 compressionTime;
        qint64 decompressionTime;
        bool success;
    };
    
    static Algorithm detectAlgorithm(const QByteArray &data);
    static QByteArray compress(const QByteArray &data, Algorithm algorithm, int level = 3);
    static QByteArray decompress(const QByteArray &compressedData, Algorithm algorithm);
    
    static QByteArray autoCompress(const QByteArray &data, int level = 3);
    static QByteArray autoDecompress(const QByteArray &compressedData);
    
    static double calculateCompressionRatio(const QByteArray &original, const QByteArray &compressed);
    static CompressionInfo benchmark(const QByteArray &data, Algorithm algorithm, int level = 3);
    static QList<CompressionInfo> benchmarkAll(const QByteArray &data, int level = 3);
    static Algorithm findBestAlgorithm(const QByteArray &data, OptimizationTarget target = BALANCED, int level = 3);
    
private:
    CompressionUtils() = delete;
};

#endif // COMPRESSION_H