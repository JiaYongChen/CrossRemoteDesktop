#include "NvJpegDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"

// CUDA device attribute constants (from cuda_runtime_api.h)
static constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75;
static constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76;

NvJpegDecoder::NvJpegDecoder() {
    m_available = probeAvailability();
    if (m_available) {
        qCInfo(lcClient) << "NvJpegDecoder: nvJPEG GPU 解码已启用";
    }
}

NvJpegDecoder::~NvJpegDecoder() {
    releaseResources();
    // DLL 随 QLibrary 析构自动卸载
}

// ---- 可用性探测 ----

bool NvJpegDecoder::probeAvailability() {
    // 1. 加载 nvJPEG DLL
    m_nvjpegLib.setFileName("nvjpeg64_12");
    if (!m_nvjpegLib.load()) {
        qCDebug(lcClient) << "NvJpegDecoder: nvjpeg64_12.dll not found"
                          << "— GPU JPEG decode unavailable";
        return false;
    }

    // 2. 加载 CUDA Runtime DLL
    m_cudaLib.setFileName("cudart64_120");
    if (!m_cudaLib.load()) {
        qCDebug(lcClient) << "NvJpegDecoder: cudart64_120.dll not found"
                          << "— GPU JPEG decode unavailable";
        return false;
    }

    // 3. 解析 CUDA 函数
    fnGetDeviceCount = reinterpret_cast<Fn_cudaGetDeviceCount>(
        m_cudaLib.resolve("cudaGetDeviceCount"));
    fnGetDevice = reinterpret_cast<Fn_cudaGetDevice>(
        m_cudaLib.resolve("cudaGetDevice"));
    fnDeviceGetAttr = reinterpret_cast<Fn_cudaDeviceGetAttribute>(
        m_cudaLib.resolve("cudaDeviceGetAttribute"));
    fnMalloc = reinterpret_cast<Fn_cudaMalloc>(
        m_cudaLib.resolve("cudaMalloc"));
    fnCudaFree = reinterpret_cast<Fn_cudaFree>(
        m_cudaLib.resolve("cudaFree"));
    fnMemcpy = reinterpret_cast<Fn_cudaMemcpy>(
        m_cudaLib.resolve("cudaMemcpy"));
    fnStreamCreate = reinterpret_cast<Fn_cudaStreamCreate>(
        m_cudaLib.resolve("cudaStreamCreate"));
    fnStreamDestroy = reinterpret_cast<Fn_cudaStreamDestroy>(
        m_cudaLib.resolve("cudaStreamDestroy"));
    fnStreamSync = reinterpret_cast<Fn_cudaStreamSynchronize>(
        m_cudaLib.resolve("cudaStreamSynchronize"));

    if (!fnGetDeviceCount || !fnGetDevice || !fnDeviceGetAttr
        || !fnMalloc || !fnCudaFree || !fnMemcpy
        || !fnStreamCreate || !fnStreamDestroy || !fnStreamSync) {
        qCDebug(lcClient) << "NvJpegDecoder: CUDA symbols not resolved";
        return false;
    }

    // 4. 解析 nvJPEG 函数
    fnCreateSimple = reinterpret_cast<Fn_nvjpegCreateSimple>(
        m_nvjpegLib.resolve("nvjpegCreateSimple"));
    fnJpegStateCreate = reinterpret_cast<Fn_nvjpegJpegStateCreate>(
        m_nvjpegLib.resolve("nvjpegJpegStateCreate"));
    fnJpegStateDestroy = reinterpret_cast<Fn_nvjpegJpegStateDestroy>(
        m_nvjpegLib.resolve("nvjpegJpegStateDestroy"));
    fnGetImageInfo = reinterpret_cast<Fn_nvjpegGetImageInfo>(
        m_nvjpegLib.resolve("nvjpegGetImageInfo"));
    fnDecode = reinterpret_cast<Fn_nvjpegDecode>(
        m_nvjpegLib.resolve("nvjpegDecode"));
    fnDestroy = reinterpret_cast<Fn_nvjpegDestroy>(
        m_nvjpegLib.resolve("nvjpegDestroy"));

    if (!fnCreateSimple || !fnJpegStateCreate || !fnJpegStateDestroy
        || !fnGetImageInfo || !fnDecode || !fnDestroy) {
        qCDebug(lcClient) << "NvJpegDecoder: nvJPEG symbols not resolved";
        return false;
    }

    // 5. 检查 CUDA 设备
    int deviceCount = 0;
    if (fnGetDeviceCount(&deviceCount) != CUDA_SUCCESS || deviceCount == 0) {
        qCDebug(lcClient) << "NvJpegDecoder: no CUDA-capable GPUs found";
        return false;
    }

    // 6. 检查首选设备计算能力（nvJPEG 要求 CC >= 5.0 / Maxwell）
    int device = 0;
    fnGetDevice(&device);
    int ccMajor = 0, ccMinor = 0;
    fnDeviceGetAttr(&ccMajor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device);
    fnDeviceGetAttr(&ccMinor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device);

    if (ccMajor < 5) {
        qCDebug(lcClient) << "NvJpegDecoder: GPU CC" << ccMajor << "." << ccMinor
                          << "too old (need >= 5.0) — nvJPEG requires Maxwell+";
        return false;
    }

    qCInfo(lcClient) << "NvJpegDecoder: CUDA device" << device
                     << "CC" << ccMajor << "." << ccMinor << "— nvJPEG ready";
    m_librariesLoaded = true;
    return true;
}

// ---- 延迟初始化 nvJPEG 句柄（首次 decode 调用时） ----

bool NvJpegDecoder::ensureInitialized() {
    if (m_initialized) return true;
    if (!m_librariesLoaded) return false;

    // 创建 nvJPEG 句柄
    if (fnCreateSimple(&m_handle) != NVJPEG_STATUS_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: nvjpegCreateSimple failed";
        return false;
    }

    // 创建解码状态（跨帧复用）
    if (fnJpegStateCreate(m_handle, &m_state) != NVJPEG_STATUS_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: nvjpegJpegStateCreate failed";
        fnDestroy(m_handle);
        m_handle = nullptr;
        return false;
    }

    // 创建 CUDA 流
    if (fnStreamCreate(&m_stream) != CUDA_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: cudaStreamCreate failed";
        fnJpegStateDestroy(m_state);
        m_state = nullptr;
        fnDestroy(m_handle);
        m_handle = nullptr;
        return false;
    }

    m_initialized = true;
    return true;
}

// ---- 资源释放 ----

void NvJpegDecoder::releaseResources() {
    if (m_stream) {
        fnStreamDestroy(m_stream);
        m_stream = nullptr;
    }
    if (m_state) {
        fnJpegStateDestroy(m_state);
        m_state = nullptr;
    }
    if (m_handle) {
        fnDestroy(m_handle);
        m_handle = nullptr;
    }
    if (m_dBuffer) {
        fnCudaFree(m_dBuffer);
        m_dBuffer = nullptr;
        m_dBufferSize = 0;
    }
    m_initialized = false;
}

// ---- CPU 回退 decode() —— 标准路径 ----

bool NvJpegDecoder::decode(
    const QByteArray& jpegData,
    QImage& output,
    int* outWidth,
    int* outHeight)
{
    if (!m_librariesLoaded) return false;
    if (!ensureInitialized()) return false;

    const auto* src = reinterpret_cast<const unsigned char*>(jpegData.constData());
    const size_t srcLen = static_cast<size_t>(jpegData.size());

    // 1. 读取 JPEG 头部
    int nComponents = 0, subsampling = 0;
    int widths[4] = {}, heights[4] = {};
    if (fnGetImageInfo(m_handle, src, srcLen,
                       &nComponents, &subsampling, widths, heights)
        != NVJPEG_STATUS_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: nvjpegGetImageInfo failed";
        return false;
    }

    const int w = widths[0];   // Y/亮度 通道宽度
    const int h = heights[0];  // Y/亮度 通道高度
    if (w <= 0 || h <= 0) {
        qCWarning(lcClient) << "NvJpegDecoder: invalid JPEG dimensions" << w << "x" << h;
        return false;
    }

    // 2. 分配/复用 GPU 输出缓冲
    constexpr int kRGB = 3;
    const size_t needed = static_cast<size_t>(w) * h * kRGB;
    if (!m_dBuffer || m_dBufferSize < needed) {
        if (m_dBuffer) fnCudaFree(m_dBuffer);
        // 4 字节对齐，避免 NVJPEG 内部断言
        const size_t alignedPitch = ((static_cast<size_t>(w) * kRGB + 3) / 4) * 4;
        const size_t alignedSize = alignedPitch * h;
        if (fnMalloc(reinterpret_cast<void**>(&m_dBuffer), alignedSize)
            != CUDA_SUCCESS) {
            qCWarning(lcClient) << "NvJpegDecoder: cudaMalloc failed for"
                                << w << "x" << h;
            m_dBuffer = nullptr;
            m_dBufferSize = 0;
            return false;
        }
        m_dBufferSize = alignedSize;
    }

    // 3. 设置输出目标
    NvJpegImage dst{};
    dst.channel[0] = m_dBuffer;
    dst.pitch[0]   = static_cast<size_t>(w) * kRGB;  // 行步长 = 宽度 × 3 字节

    // 4. 异步解码
    if (fnDecode(m_handle, m_state, src, srcLen,
                 NVJPEG_OUTPUT_RGBI, &dst, m_stream)
        != NVJPEG_STATUS_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: nvjpegDecode failed";
        return false;
    }

    // 5. 等待 GPU 完成
    if (fnStreamSync(m_stream) != CUDA_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: cudaStreamSynchronize failed";
        return false;
    }

    // 6. 从 GPU 复制到 CPU QImage
    if (output.isNull() || output.width() != w || output.height() != h) {
        output = QImage(w, h, QImage::Format_RGB888);
    }
    // cudaMemcpyDeviceToHost = 2: GPU → CPU
    static constexpr int CUDA_MEMCPY_DEVICE_TO_HOST = 2;
    if (fnMemcpy(output.bits(), m_dBuffer, needed,
                 CUDA_MEMCPY_DEVICE_TO_HOST) != CUDA_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: cudaMemcpy D2H failed";
        return false;
    }

    *outWidth  = w;
    *outHeight = h;
    return true;
}

// ---- 零拷贝 decodeToPBO() —— GPU → PBO 路径 ----

bool NvJpegDecoder::decodeToPBO(
    const QByteArray& jpegData,
    unsigned char* pboPtr,
    int width, int height,
    int pixelSize)
{
    if (!m_librariesLoaded) return false;
    if (!ensureInitialized()) return false;
    Q_UNUSED(pixelSize);

    const auto* src = reinterpret_cast<const unsigned char*>(jpegData.constData());
    const size_t srcLen = static_cast<size_t>(jpegData.size());

    // 1. 验证尺寸匹配（调用方保证）
    int nComponents = 0, subsampling = 0;
    int widths[4] = {}, heights[4] = {};
    if (fnGetImageInfo(m_handle, src, srcLen,
                       &nComponents, &subsampling, widths, heights)
        != NVJPEG_STATUS_SUCCESS) {
        return false;
    }
    if (widths[0] != width || heights[0] != height) {
        qCWarning(lcClient) << "NvJpegDecoder: size mismatch in decodeToPBO";
        return false;
    }

    // 2. 分配临时 GPU 缓冲
    constexpr int kRGB = 3;
    const size_t needed = static_cast<size_t>(width) * height * kRGB;
    unsigned char* tempBuffer = nullptr;
    if (fnMalloc(reinterpret_cast<void**>(&tempBuffer), needed) != CUDA_SUCCESS) {
        return false;
    }

    // 3. 解码到临时 GPU 缓冲
    NvJpegImage dst{};
    dst.channel[0] = tempBuffer;
    dst.pitch[0]   = static_cast<size_t>(width) * kRGB;

    if (fnDecode(m_handle, m_state, src, srcLen,
                 NVJPEG_OUTPUT_RGBI, &dst, m_stream)
        != NVJPEG_STATUS_SUCCESS) {
        fnCudaFree(tempBuffer);
        return false;
    }
    fnStreamSync(m_stream);

    // 4. GPU → PBO（两者都在 GPU 端，DMA 传输，不经过 CPU）
    //    cudaMemcpyDeviceToDevice = 3
    if (fnMemcpy(pboPtr, tempBuffer, needed, CUDA_MEMCPY_DEVICE_TO_DEVICE)
        != CUDA_SUCCESS) {
        fnCudaFree(tempBuffer);
        return false;
    }

    // 5. 释放临时缓冲
    fnCudaFree(tempBuffer);
    return true;
}
