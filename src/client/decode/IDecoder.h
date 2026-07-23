#pragma once

#include <QtCore/QByteArray>
#include <QtGui/QImage>

#include <QtGui/qopengl.h>
class IDecodeTarget;

/**
 * @brief JPEG 解码器抽象接口（v2 — 统一 decode）
 *
 * 解码器在 DecodeWorker::start() 中按优先级选择：
 *   1. NvJpegDecoder（NVIDIA GPU CC >= 5.0）
 *   2. TurboJpegDecoder（CPU 回退，始终可用）
 *
 * 调用方只需调用 decode()，内部自行决定最优路径：
 *   - GPU 解码器：mapWriteBuffer → GPU 解码直写 PBO → commitWriteBuffer
 *   - CPU 解码器：CPU 解码 → uploadPixels
 *
 * 所有实现必须线程安全——decode() 在解码线程调用。
 */
class IDecoder {
public:
    virtual ~IDecoder() = default;

    /// 解码器名称，用于诊断日志（如 "libjpeg-turbo"、"nvJPEG"）
    [[nodiscard]] virtual const char* name() const = 0;

    /// 构造后注入解码输出目标（首次 decode 前调用一次）
    /// GPU 解码器通过 target 写入纹理；CPU 解码器通过 target 上传像素
    virtual void setDecodeTarget(IDecodeTarget* target) { m_target = target; }

    /// JPEG 解码 + 纹理上传（统一入口）
    ///
    /// 各实现内部自行选择最优路径：
    ///   - TurboJpegDecoder: tjDecompress2 -> target->uploadPixels()
    ///   - NvJpegDecoder:    target->mapWriteBuffer() -> nvjpegDecode -> D2D -> target->commitWriteBuffer()
    ///
    /// 回退策略：
    ///   - NvJpegDecoder GPU 失败时内部回退到 TurboJpegDecoder（惰性构造）
    ///
    /// @param jpegData     原始 JPEG 字节（必须包含有效 JPEG 头）
    /// @param[out] outWidth   解码后图像宽度
    /// @param[out] outHeight  解码后图像高度
    /// @param[out] outFence   GLsync fence（GUI 线程同步用），GPU 路径失败时为 nullptr
    /// @param[out] outImage   CPU 回退：当 outFence 为 nullptr 时，解码器将像素写入此 QImage
    /// @return 解码是否成功
    [[nodiscard]] virtual bool decode(const QByteArray& jpegData,
                                       int* outWidth,
                                       int* outHeight,
                                       GLsync* outFence,
                                       QImage* outImage = nullptr) = 0;

protected:
    IDecodeTarget* m_target = nullptr;  ///< 解码输出目标（构造后注入）
};
