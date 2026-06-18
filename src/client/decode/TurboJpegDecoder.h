#pragma once

#include "IDecoder.h"
#include <turbojpeg.h>

/**
 * @brief libjpeg-turbo CPU 解码器（始终可用）
 *
 * 将现有 tjDecompress2 调用封装为 IDecoder 接口。
 * 内部管理 tjhandle 生命周期和 QImage 缓冲复用。
 */
class TurboJpegDecoder : public IDecoder {
public:
    TurboJpegDecoder();
    ~TurboJpegDecoder() override;

    [[nodiscard]] bool isAvailable() const override { return true; }

#ifndef QT_NO_OPENGL
    [[nodiscard]] bool decode(const QByteArray& jpegData,
                               int* outWidth,
                               int* outHeight,
                               GLsync* outFence,
                               QImage* outImage = nullptr) override;
#else
    [[nodiscard]] bool decode(const QByteArray& jpegData,
                               int* outWidth,
                               int* outHeight,
                               QImage* outImage) override;
#endif

    [[nodiscard]] const char* name() const override { return "libjpeg-turbo"; }

private:
    tjhandle m_handle = nullptr;
    QImage   m_buffer;  ///< CPU 解码缓冲复用
};
