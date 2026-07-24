#pragma once

#include <QtGui/QImage>

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

    [[nodiscard]] bool decode(const QByteArray& jpegData,
                               int* outWidth,
                               int* outHeight,
                               GLsync* outFence,
                               QImage* outImage = nullptr) override;

    [[nodiscard]] const char* name() const override { return "libjpeg-turbo"; }

private:
    tjhandle m_handle = nullptr;
    QImage   m_buffer;  ///< CPU 解码缓冲复用
};
