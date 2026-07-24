// VideoToolboxDecoder.h — macOS GPU JPEG 解码器
#pragma once

#include <memory>

#include "client/decode/IDecoder.h"

#ifdef Q_OS_MACOS
/**
 * @brief macOS VideoToolbox + CoreGraphics GPU JPEG 解码器
 *
 * 通过 ImageIO + CoreGraphics + PBO 实现 JPEG 解码到 GPU 纹理的管线，
 * 最小化 CPU-GPU 同步开销。并非严格意义上的硬件 JPEG 解码
 * （VideoToolbox 面向 H.264/HEVC 等视频编解码格式），
 * 但通过 CoreGraphics 将解压卸载到 GPU 的纹理上传管线。
 *
 * 回退策略：解码失败时返回 false，调用方降级到 TurboJpegDecoder (CPU)。
 */
class VideoToolboxDecoder : public IDecoder {
public:
    VideoToolboxDecoder();
    ~VideoToolboxDecoder() override;

    [[nodiscard]] const char* name() const override { return "VideoToolbox"; }
    [[nodiscard]] bool decode(const QByteArray& jpegData,
                               int* outWidth,
                               int* outHeight,
                               GLsync* outFence,
                               QImage* outImage = nullptr) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // Q_OS_MACOS
