#pragma once

#include <QtCore/QByteArray>
#include <QtGui/QImage>

/**
 * @brief JPEG 解码器抽象接口
 *
 * 解码器在运行时通过 DecodeWorker::start() 选择：
 * - 若 nvJPEG 可用（NVIDIA GPU CC >= 5.0），使用 NvJpegDecoder
 * - 否则回退到 TurboJpegDecoder
 *
 * 所有实现必须线程安全——decode() 在解码线程调用。
 */
class IDecoder {
public:
    virtual ~IDecoder() = default;

    /// 检查当前硬件/运行时环境是否支持此解码器
    /// NvJpegDecoder 会检测 CUDA 设备；TurboJpegDecoder 始终返回 true
    [[nodiscard]] virtual bool isAvailable() const = 0;

    /// 解码 JPEG 字节到 RGB 像素缓冲区
    /// @param jpegData 原始 JPEG 字节（必须包含有效 JPEG 头）
    /// @param output 输出 QImage。调用方可复用同一 QImage 缓冲；
    ///               实现负责仅在尺寸变化时重建。
    /// @param[out] outWidth 解码后图像宽度
    /// @param[out] outHeight 解码后图像高度
    /// @return 解码是否成功
    [[nodiscard]] virtual bool decode(
        const QByteArray& jpegData,
        QImage& output,
        int* outWidth,
        int* outHeight) = 0;

    /// 零拷贝 PBO 路径：解码直接写入已映射的 GL PBO 指针
    /// 仅 GPU 解码器可重写。CPU 解码器返回 false 以使用标准 memcpy 路径。
    /// @param jpegData 原始 JPEG 字节
    /// @param pboPtr 由 GL 映射的持久 PBO 指针（可写）
    /// @param width 期望的图像宽度
    /// @param height 期望的图像高度
    /// @param pixelSize 每像素字节数（RGB = 3）
    /// @return true 表示已直接写入 PBO，调用方跳过 memcpy
    [[nodiscard]] virtual bool decodeToPBO(
        const QByteArray& jpegData,
        unsigned char* pboPtr,
        int width, int height,
        int pixelSize)
    {
        Q_UNUSED(jpegData);
        Q_UNUSED(pboPtr);
        Q_UNUSED(width);
        Q_UNUSED(height);
        Q_UNUSED(pixelSize);
        return false;
    }

    /// 解码器名称，用于诊断日志（如 "libjpeg-turbo"、"nvJPEG"）
    [[nodiscard]] virtual const char* name() const = 0;
};
