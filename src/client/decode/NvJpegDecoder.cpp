#include "NvJpegDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"

// CUDA 设备属性常量（运行时查询，无需 CUDA 头文件）
// 参考: cuda_runtime_api.h CUdevice_attribute_enum
static constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75;
static constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76;

NvJpegDecoder::NvJpegDecoder() {
    m_available = probeAvailability();
    if (m_available) {
        qCInfo(lcClient) << "NvJpegDecoder: nvJPEG GPU 解码已启用";
    }
}

NvJpegDecoder::~NvJpegDecoder() {
    // DLL 随 QLibrary 析构自动卸载
}

bool NvJpegDecoder::probeAvailability() {
    // 1. 尝试加载 nvJPEG DLL
    m_nvjpegLib.setFileName("nvjpeg64_12");
    if (!m_nvjpegLib.load()) {
        qCDebug(lcClient) << "NvJpegDecoder: nvjpeg64_12.dll not found,"
                          << "GPU JPEG decode unavailable";
        return false;
    }

    // 2. 尝试加载 CUDA Runtime DLL
    m_cudaLib.setFileName("cudart64_120");
    if (!m_cudaLib.load()) {
        qCDebug(lcClient) << "NvJpegDecoder: cudart64_120.dll not found,"
                          << "GPU JPEG decode unavailable";
        return false;
    }

    // 3. 解析 CUDA 函数指针
    fnGetDeviceCount = reinterpret_cast<Fn_cudaGetDeviceCount>(
        m_cudaLib.resolve("cudaGetDeviceCount"));
    fnGetDevice = reinterpret_cast<Fn_cudaGetDevice>(
        m_cudaLib.resolve("cudaGetDevice"));
    fnDeviceGetAttr = reinterpret_cast<Fn_cudaDeviceGetAttribute>(
        m_cudaLib.resolve("cudaDeviceGetAttribute"));
    fnFree = reinterpret_cast<Fn_cudaFree>(
        m_cudaLib.resolve("cudaFree"));

    if (!fnGetDeviceCount || !fnGetDevice || !fnDeviceGetAttr || !fnFree) {
        qCDebug(lcClient) << "NvJpegDecoder: CUDA symbols not resolved";
        return false;
    }

    // 4. 检查是否有 CUDA 设备
    int deviceCount = 0;
    if (fnGetDeviceCount(&deviceCount) != 0 || deviceCount == 0) {
        qCDebug(lcClient) << "NvJpegDecoder: no CUDA-capable GPUs found";
        return false;
    }

    // 5. 检查首选设备的计算能力
    int device = 0;
    fnGetDevice(&device);
    int ccMajor = 0, ccMinor = 0;
    fnDeviceGetAttr(&ccMajor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device);
    fnDeviceGetAttr(&ccMinor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device);

    if (ccMajor < 5) {  // nvJPEG 要求 Maxwell (CC 5.0) 及以上
        qCDebug(lcClient) << "NvJpegDecoder: GPU CC" << ccMajor << "." << ccMinor
                          << "is too old (need >= 5.0)";
        return false;
    }

    qCInfo(lcClient) << "NvJpegDecoder: CUDA device" << device
                     << "CC" << ccMajor << "." << ccMinor << "detected";
    m_librariesLoaded = true;
    return true;
}

bool NvJpegDecoder::decode(
    const QByteArray& jpegData,
    QImage& output,
    int* outWidth,
    int* outHeight)
{
    Q_UNUSED(jpegData);
    Q_UNUSED(output);
    Q_UNUSED(outWidth);
    Q_UNUSED(outHeight);
    // 骨架：nvJPEG API 调用在后续任务中实现
    // 当前若误调用则通过 DecodeWorker 的错误处理优雅回退
    qCWarning(lcClient) << "NvJpegDecoder::decode() not yet implemented";
    return false;
}

bool NvJpegDecoder::decodeToPBO(
    const QByteArray& jpegData,
    unsigned char* pboPtr,
    int width, int height,
    int pixelSize)
{
    Q_UNUSED(jpegData);
    Q_UNUSED(pboPtr);
    Q_UNUSED(width);
    Q_UNUSED(height);
    Q_UNUSED(pixelSize);
    qCWarning(lcClient) << "NvJpegDecoder::decodeToPBO() not yet implemented";
    return false;
}
