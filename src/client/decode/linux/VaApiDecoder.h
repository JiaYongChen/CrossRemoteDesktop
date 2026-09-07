// VaApiDecoder.h — Linux VA-API GPU JPEG 解码器
#pragma once

#include "client/decode/IDecoder.h"

#if defined(Q_OS_LINUX) && defined(HAS_VAAPI)
/**
 * @brief Linux VA-API GPU JPEG 解码器占位实现
 *
 * 当前尚未实现 JPEG 参数解析、VAContext 生命周期和 YUV 到 RGB 转换，
 * 因此始终报告不可用。DecodeWorker 会选择 TurboJpegDecoder，避免把
 * 不完整的硬件路径暴露给运行时。
 */
class VaApiDecoder : public IDecoder {
public:
    VaApiDecoder();
    ~VaApiDecoder() override = default;

    [[nodiscard]] const char* name() const override { return "VA-API"; }
    [[nodiscard]] bool decode(const QByteArray& jpegData,
                               int* outWidth,
                               int* outHeight,
                               GLsync* outFence,
                               QImage* outImage = nullptr) override;

    /// 完整实现接入前始终返回 false。
    [[nodiscard]] bool isAvailable() const { return false; }
};

#endif // defined(Q_OS_LINUX) && defined(HAS_VAAPI)
