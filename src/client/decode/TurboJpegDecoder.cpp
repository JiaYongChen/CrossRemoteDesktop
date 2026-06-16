#include "TurboJpegDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"

TurboJpegDecoder::TurboJpegDecoder() {
    m_handle = tjInitDecompress();
    if (!m_handle) {
        qCWarning(lcClient) << "TurboJpegDecoder: tjInitDecompress failed";
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
    QImage& output,
    int* outWidth,
    int* outHeight)
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
    if (output.isNull() || output.width() != w || output.height() != h) {
        output = QImage(w, h, QImage::Format_RGB888);
    }

    // 3. 解码
    if (tjDecompress2(m_handle, src, srcSize, output.bits(),
                      w, 0, h, TJPF_RGB, TJFLAG_FASTDCT) != 0) {
        qCWarning(lcClient) << "TurboJpegDecoder: tjDecompress2 failed:"
                            << tjGetErrorStr2(m_handle);
        return false;
    }

    *outWidth = w;
    *outHeight = h;
    return true;
}
