// VaApiDecoder.h — Linux VA-API GPU JPEG 解码器
#pragma once

#ifdef Q_OS_LINUX

#include "../IDecoder.h"
#include <memory>

/**
 * @brief Linux VA-API GPU JPEG 解码器
 *
 * 通过 VA-API（Video Acceleration API）调用 GPU 硬件 JPEG 解码引擎，
 * 将 YUV 解码结果上传到 GL 纹理。
 *
 * 回退策略：构造函数探测 VA-API 可用性，不可用时 isAvailable() 返回 false，
 * 调用方降级到 TurboJpegDecoder (CPU)。
 *
 * 系统要求：Linux + VA-API 驱动（Intel iHD/i965 或 AMD Mesa VA）
 * DRI 设备：/dev/dri/renderD128 或 /dev/dri/card0
 */
class VaApiDecoder : public IDecoder {
public:
    VaApiDecoder();
    ~VaApiDecoder() override;

    [[nodiscard]] const char* name() const override { return "VA-API"; }
    [[nodiscard]] bool decode(const QByteArray& jpegData,
                               int* outWidth,
                               int* outHeight,
                               GLsync* outFence,
                               QImage* outImage = nullptr) override;

    /// 运行时检测 VA-API 是否可用
    [[nodiscard]] bool isAvailable() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_available = false;
};

#endif // Q_OS_LINUX
