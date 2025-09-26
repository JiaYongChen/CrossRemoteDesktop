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
#include "../config/Constants.h"

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
    
    // 图像格式检测和预判
    static ImageFormat detectImageFormat(const QByteArray &imageData);
    static bool isJpegData(const QByteArray &data);
    static bool isPngData(const QByteArray &data);
    static bool isBmpData(const QByteArray &data);
    static bool isWebpData(const QByteArray &data);
    static bool isTiffData(const QByteArray &data);
    static QString getImageFormatName(const QByteArray &imageData);
    
    // 自适应压缩策略
    struct ImageAnalysis {
        double complexity;      // 图像复杂度 (0.0-1.0)
        double colorVariance;   // 颜色方差
        bool hasTransparency;   // 是否包含透明度
        int uniqueColors;       // 唯一颜色数量
        QSize imageSize;        // 图像尺寸
    };
    
    static ImageAnalysis analyzeImage(const QImage &image);
    static ImageFormat selectOptimalFormat(const QImage &image);
    static int selectOptimalQuality(const QImage &image, ImageFormat format);
    static QByteArray adaptiveCompressImage(const QImage &image);
    
    // 数据完整性验证
    static quint32 calculateCRC32(const QByteArray &data);
    static QByteArray calculateMD5(const QByteArray &data);
    static bool validateDataIntegrity(const QByteArray &data, quint32 expectedCRC);
    static bool validateDataIntegrity(const QByteArray &data, const QByteArray &expectedMD5);
    static QByteArray addDataChecksum(const QByteArray &data);
    static QPair<QByteArray, bool> extractAndValidateData(const QByteArray &dataWithChecksum);
    
    // 增强的回退机制
    struct FallbackResult {
        QByteArray data;           // 处理后的数据
        bool usedFallback;         // 是否使用了回退机制
        QString errorMessage;      // 错误信息（如果有）
        int attemptCount;          // 尝试次数
    };
    
    static FallbackResult robustApplyDifference(const QByteArray &previous, const QByteArray &difference);
    static bool isValidImageData(const QByteArray &data);
    static QByteArray repairCorruptedData(const QByteArray &data);
    static FallbackResult processWithFallback(const QByteArray &data, const QByteArray &previousFrame);
    
    // 错误处理
    static QString lastError();
    
private:
    // 内部压缩器接口
    class ICompressor {
    public:
        virtual ~ICompressor() = default;
        virtual QByteArray compress(const QByteArray& input, int level = -1) = 0;
        virtual QByteArray decompress(const QByteArray& input) = 0;
    };
    
    // 具体压缩器实现类
    class ZlibCompressor;
    class LZ4Compressor;
    class ZstdCompressor;
    
    // 获取压缩器实例的方法
    static ICompressor* getCompressor(Algorithm algorithm);
    
    // 私有压缩方法
    static QByteArray compressZlib(const QByteArray &data, Level level);
    static QByteArray decompressZlib(const QByteArray &compressedData);
    
    // GZIP压缩方法
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

#endif // COMPRESSION_H