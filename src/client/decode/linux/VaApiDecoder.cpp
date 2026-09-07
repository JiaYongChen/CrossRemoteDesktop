// VaApiDecoder.cpp — Linux VA-API GPU JPEG 解码器占位实现
#include "VaApiDecoder.h"

#if defined(Q_OS_LINUX) && defined(HAS_VAAPI)
#include "common/logging/LoggingCategories.h"

VaApiDecoder::VaApiDecoder()
{
    qCDebug(lcClientSessionDecode)
        << "VA-API JPEG 解码尚未完成，使用 libjpeg-turbo";
}

bool VaApiDecoder::decode(const QByteArray& jpegData,
                          int* outWidth,
                          int* outHeight,
                          GLsync* outFence,
                          QImage* outImage)
{
    Q_UNUSED(jpegData);
    Q_UNUSED(outImage);

    if (outWidth) {
        *outWidth = 0;
    }
    if (outHeight) {
        *outHeight = 0;
    }
    if (outFence) {
        *outFence = nullptr;
    }
    return false;
}

#endif // defined(Q_OS_LINUX) && defined(HAS_VAAPI)
