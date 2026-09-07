#pragma once

#include "client/decode/IDecoder.h"

#ifdef Q_OS_MACOS

/**
 * @brief macOS ImageIO JPEG 解码器
 *
 * ImageIO/CoreGraphics 解码为 RGB 图像；存在 OpenGL 解码目标时沿用统一上传
 * 接口，否则通过 outImage 返回 CPU 图像。
 */
class MacOSImageDecoder final : public IDecoder {
public:
    [[nodiscard]] const char* name() const override { return "ImageIO"; }

    [[nodiscard]] bool decode(const QByteArray& jpegData,
                              int* outWidth,
                              int* outHeight,
                              GLsync* outFence,
                              QImage* outImage = nullptr) override;
};

#endif // Q_OS_MACOS
