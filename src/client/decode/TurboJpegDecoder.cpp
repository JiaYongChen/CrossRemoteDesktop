#include "TurboJpegDecoder.h"
#include "../../common/logging/LoggingCategories.h"

#include "IDecodeTarget.h"

TurboJpegDecoder::TurboJpegDecoder() {
    m_handle = tjInitDecompress();
    if (!m_handle) {
        qCCritical(lcClient) << "TurboJpegDecoder: tjInitDecompress failed";
    }
}

TurboJpegDecoder::~TurboJpegDecoder() {
    if (m_handle) {
        tjDestroy(m_handle);
        m_handle = nullptr;
    }
}

bool TurboJpegDecoder::decode(
    const QByteArray& jpegData,
    int* outWidth,
    int* outHeight,
    GLsync* outFence,
    QImage* outImage)
{
    if (!m_handle) {
        qCWarning(lcClient) << "TurboJpegDecoder: not initialized";
        return false;
    }

    const unsigned char* src =
        reinterpret_cast<const unsigned char*>(jpegData.constData());
    const unsigned long srcSize =
        static_cast<unsigned long>(jpegData.size());

    // 1. 读取 JPEG 头部
    int w = 0, h = 0, subsamp = 0, cs = 0;
    if (tjDecompressHeader3(m_handle, src, srcSize, &w, &h, &subsamp, &cs) != 0) {
        qCWarning(lcClient) << "TurboJpegDecoder: tjDecompressHeader3 failed:"
                            << tjGetErrorStr2(m_handle);
        return false;
    }

    // 2. 复用或重建输出缓冲
    if (m_buffer.isNull() || m_buffer.width() != w || m_buffer.height() != h) {
        m_buffer = QImage(w, h, QImage::Format_RGB888);
    }

    // 3. CPU 解码
    if (tjDecompress2(m_handle, src, srcSize, m_buffer.bits(),
                      w, 0, h, TJPF_RGB, TJFLAG_FASTDCT) != 0) {
        qCWarning(lcClient) << "TurboJpegDecoder: tjDecompress2 failed:"
                            << tjGetErrorStr2(m_handle);
        return false;
    }

    *outWidth = w;
    *outHeight = h;

    // 4. 通过 target 上传到 GL 纹理（如果可用）
    if (m_target) {
        *outFence = m_target->uploadPixels(m_buffer.bits(), w, h);
    } else {
        *outFence = nullptr;
    }

    // 5. 填充回退 QImage（调用方在 outFence==nullptr 时使用）
    if (outImage) {
        *outImage = m_buffer;
    }

    return true;
}
