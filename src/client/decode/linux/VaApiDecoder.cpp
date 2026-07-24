// VaApiDecoder.cpp — Linux VA-API GPU JPEG 解码器实现
#include "VaApiDecoder.h"

#ifdef Q_OS_LINUX
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <QtCore/QDebug>
#include <QtCore/QFile>

#include "client/decode/IDecodeTarget.h"
#include "common/logging/LoggingCategories.h"

#include <va/va.h>
#include <va/va_drm.h>

struct VaApiDecoder::Impl {
    int driFd = -1;
    VADisplay vaDisplay = nullptr;
    VAConfigID vaConfig = VA_INVALID_ID;
    VAContextID vaContext = VA_INVALID_ID;
};

VaApiDecoder::VaApiDecoder()
    : m_impl(std::make_unique<Impl>())
{
    // 探测 VA-API 可用性
    m_impl->driFd = open("/dev/dri/renderD128", O_RDWR);
    if (m_impl->driFd < 0) {
        m_impl->driFd = open("/dev/dri/card0", O_RDWR);
    }
    if (m_impl->driFd < 0) {
        qCDebug(lcClientSessionDecode) << "VA-API: 无法打开 DRI 设备";
        return;
    }

    m_impl->vaDisplay = vaGetDisplayDRM(m_impl->driFd);
    if (!m_impl->vaDisplay) {
        qCDebug(lcClientSessionDecode) << "VA-API: vaGetDisplayDRM 失败";
        close(m_impl->driFd);
        m_impl->driFd = -1;
        return;
    }

    int major = 0, minor = 0;
    VAStatus status = vaInitialize(m_impl->vaDisplay, &major, &minor);
    if (status != VA_STATUS_SUCCESS) {
        qCDebug(lcClientSessionDecode) << "VA-API: 初始化失败" << status;
        close(m_impl->driFd);
        m_impl->driFd = -1;
        m_impl->vaDisplay = nullptr;
        return;
    }

    // 创建 JPEG 解码配置
    VAEntrypoint entrypoints[2];
    int numEntrypoints = 0;
    status = vaQueryConfigEntrypoints(m_impl->vaDisplay, VAProfileJPEGBaseline,
                                       entrypoints, &numEntrypoints);
    if (status != VA_STATUS_SUCCESS || numEntrypoints < 1) {
        qCDebug(lcClientSessionDecode) << "VA-API: JPEG 解码不支持";
        return;
    }

    status = vaCreateConfig(m_impl->vaDisplay, VAProfileJPEGBaseline,
                             VAEntrypointVLD, nullptr, 0, &m_impl->vaConfig);
    if (status != VA_STATUS_SUCCESS) {
        qCDebug(lcClientSessionDecode) << "VA-API: 创建配置失败" << status;
        return;
    }

    m_available = true;
    qCInfo(lcClientSessionDecode) << "VA-API JPEG 解码器已就绪"
                                  << "v" << major << "." << minor;
}

VaApiDecoder::~VaApiDecoder()
{
    if (m_impl->vaContext != VA_INVALID_ID) {
        vaDestroyContext(m_impl->vaDisplay, m_impl->vaContext);
    }
    if (m_impl->vaConfig != VA_INVALID_ID) {
        vaDestroyConfig(m_impl->vaDisplay, m_impl->vaConfig);
    }
    if (m_impl->vaDisplay) {
        vaTerminate(m_impl->vaDisplay);
    }
    if (m_impl->driFd >= 0) {
        close(m_impl->driFd);
    }
}

bool VaApiDecoder::isAvailable() const
{
    return m_available;
}

bool VaApiDecoder::decode(const QByteArray& jpegData,
                            int* outWidth,
                            int* outHeight,
                            GLsync* outFence,
                            QImage* outImage)
{
    Q_UNUSED(outImage);

    if (!m_available || !m_target)
        return false;

    // 创建 VA surface
    // 注：实际实现需要先解析 JPEG 头获取宽高，此处展示架构流程
    VASurfaceID surfaceId = VA_INVALID_ID;
    int width = 1920;   // 占位——需从 JPEG 头解析
    int height = 1080;  // 占位——需从 JPEG 头解析

    VAStatus status = vaCreateSurfaces(m_impl->vaDisplay,
                                        VA_RT_FORMAT_YUV420,
                                        width, height,
                                        &surfaceId, 1, nullptr, 0);
    if (status != VA_STATUS_SUCCESS) {
        return false;
    }

    // 创建 buffer 并填充 JPEG 数据
    VABufferID codedBuf = VA_INVALID_ID;
    status = vaCreateBuffer(m_impl->vaDisplay, m_impl->vaContext,
                             VACodedBufferType,
                             static_cast<unsigned int>(jpegData.size()), 1,
                             const_cast<char*>(jpegData.constData()),
                             &codedBuf);
    if (status != VA_STATUS_SUCCESS) {
        vaDestroySurfaces(m_impl->vaDisplay, &surfaceId, 1);
        return false;
    }

    // 提交解码
    VAPictureParameterBufferJPEGBaseline picParam = {};
    // 填充 picParam —— 需要解析 JPEG 头获取量化表、哈夫曼表等
    // 此处省略细节解析代码（参考 libva-utils jpegdec 示例）

    VABufferID picParamBuf = VA_INVALID_ID;
    status = vaCreateBuffer(m_impl->vaDisplay, m_impl->vaContext,
                             VAPictureParameterBufferType,
                             sizeof(picParam), 1, &picParam, &picParamBuf);
    if (status != VA_STATUS_SUCCESS) {
        vaDestroyBuffer(m_impl->vaDisplay, codedBuf);
        vaDestroySurfaces(m_impl->vaDisplay, &surfaceId, 1);
        return false;
    }

    status = vaBeginPicture(m_impl->vaDisplay, m_impl->vaContext, surfaceId);
    if (status != VA_STATUS_SUCCESS) {
        vaDestroyBuffer(m_impl->vaDisplay, picParamBuf);
        vaDestroyBuffer(m_impl->vaDisplay, codedBuf);
        vaDestroySurfaces(m_impl->vaDisplay, &surfaceId, 1);
        return false;
    }

    status = vaRenderPicture(m_impl->vaDisplay, m_impl->vaContext,
                              &picParamBuf, 1);
    status = vaRenderPicture(m_impl->vaDisplay, m_impl->vaContext,
                              &codedBuf, 1);
    vaEndPicture(m_impl->vaDisplay, m_impl->vaContext);

    // 从 VASurface 读取解码数据
    VAImage vaImage = {};
    status = vaDeriveImage(m_impl->vaDisplay, surfaceId, &vaImage);
    if (status != VA_STATUS_SUCCESS) {
        vaDestroyBuffer(m_impl->vaDisplay, codedBuf);
        vaDestroyBuffer(m_impl->vaDisplay, picParamBuf);
        vaDestroySurfaces(m_impl->vaDisplay, &surfaceId, 1);
        return false;
    }

    void* surfacePtr = nullptr;
    status = vaMapBuffer(m_impl->vaDisplay, vaImage.buf, &surfacePtr);
    if (status == VA_STATUS_SUCCESS && surfacePtr) {
        unsigned char* dst = m_target->mapWriteBuffer(
            static_cast<int>(vaImage.width),
            static_cast<int>(vaImage.height));
        if (dst) {
            // YUV→RGB 转换（简化：直接拷贝 Y 平面）
            // 生产代码需要完整的色彩空间转换
            std::memcpy(dst, surfacePtr,
                        vaImage.width * vaImage.height * 4);
            GLsync fence = m_target->commitWriteBuffer();
            if (outFence) *outFence = fence;
        }
        vaUnmapBuffer(m_impl->vaDisplay, vaImage.buf);
    }

    vaDestroyImage(m_impl->vaDisplay, vaImage.image_id);
    vaDestroyBuffer(m_impl->vaDisplay, codedBuf);
    vaDestroyBuffer(m_impl->vaDisplay, picParamBuf);
    vaDestroySurfaces(m_impl->vaDisplay, &surfaceId, 1);

    *outWidth = width;
    *outHeight = height;
    return true;
}

#endif // Q_OS_LINUX
