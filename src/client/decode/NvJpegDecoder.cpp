#include "NvJpegDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"

#ifdef HAS_NVJPEG
// ──────────────────────────────────────────────────────────────────────────
// 编译时链接 nvJPEG — 真实 GPU 解码实现
// ──────────────────────────────────────────────────────────────────────────

bool NvJpegDecoder::probeGPU() {
    int deviceCount = 0;
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0) {
        qCDebug(lcClient) << "NvJpegDecoder: no CUDA-capable GPUs found";
        return false;
    }

    int device = 0;
    cudaGetDevice(&device);
    cudaDeviceProp props{};
    if (cudaGetDeviceProperties(&props, device) != cudaSuccess) {
        qCDebug(lcClient) << "NvJpegDecoder: cudaGetDeviceProperties failed";
        return false;
    }

    if (props.major < 5) {
        qCDebug(lcClient) << "NvJpegDecoder: GPU CC" << props.major << "." << props.minor
                          << "too old (need >= 5.0) — nvJPEG requires Maxwell+";
        return false;
    }

    qCInfo(lcClient) << "NvJpegDecoder: CUDA device" << device
                     << "CC" << props.major << "." << props.minor
                     << "(" << props.name << ") — nvJPEG ready";
    return true;
}

// ── 构造 / 析构 ─────────────────────────────────────────────────────────

NvJpegDecoder::NvJpegDecoder() {
    m_available = probeGPU();
    if (m_available) {
        qCInfo(lcClient) << "NvJpegDecoder: nvJPEG GPU 解码已启用";
    }
}

NvJpegDecoder::~NvJpegDecoder() {
    releaseResources();
}

// ── 延迟初始化 nvJPEG 句柄（首次 decode 调用时）─────────────────────────

bool NvJpegDecoder::ensureInitialized() {
    if (m_initialized) return true;

    if (nvjpegCreateSimple(&m_handle) != NVJPEG_STATUS_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: nvjpegCreateSimple failed";
        return false;
    }

    if (nvjpegJpegStateCreate(m_handle, &m_state) != NVJPEG_STATUS_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: nvjpegJpegStateCreate failed";
        nvjpegDestroy(m_handle);
        m_handle = nullptr;
        return false;
    }

    if (cudaStreamCreate(&m_stream) != cudaSuccess) {
        qCWarning(lcClient) << "NvJpegDecoder: cudaStreamCreate failed";
        nvjpegJpegStateDestroy(m_state);
        m_state = nullptr;
        nvjpegDestroy(m_handle);
        m_handle = nullptr;
        return false;
    }

    m_initialized = true;
    return true;
}

// ── 资源释放 ─────────────────────────────────────────────────────────────

void NvJpegDecoder::releaseResources() {
    if (m_stream)  { cudaStreamDestroy(m_stream);  m_stream = nullptr; }
    if (m_state)   { nvjpegJpegStateDestroy(m_state); m_state = nullptr; }
    if (m_handle)  { nvjpegDestroy(m_handle);      m_handle = nullptr; }
    if (m_dBuffer) { cudaFree(m_dBuffer);          m_dBuffer = nullptr; m_dBufferSize = 0; }
    m_initialized = false;
}

// ── decode() — GPU 解码 → CPU QImage ─────────────────────────────────────

bool NvJpegDecoder::decode(
    const QByteArray& jpegData,
    QImage& output,
    int* outWidth,
    int* outHeight)
{
    if (!ensureInitialized()) return false;

    const auto* src   = reinterpret_cast<const unsigned char*>(jpegData.constData());
    const size_t srcLen = static_cast<size_t>(jpegData.size());

    // 1. 读取 JPEG 头部
    int nComponents = 0;
    nvjpegChromaSubsampling_t subsampling = NVJPEG_CSS_444;
    int widths[4] = {}, heights[4] = {};
    if (nvjpegGetImageInfo(m_handle, src, srcLen,
                           &nComponents, &subsampling, widths, heights)
        != NVJPEG_STATUS_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: nvjpegGetImageInfo failed";
        return false;
    }

    const int w = widths[0];
    const int h = heights[0];
    if (w <= 0 || h <= 0) {
        qCWarning(lcClient) << "NvJpegDecoder: invalid dimensions" << w << "x" << h;
        return false;
    }

    // 2. 分配 / 复用 GPU 输出缓冲（4 字节对齐）
    constexpr int kRGB = 3;
    const size_t needed = static_cast<size_t>(w) * h * kRGB;
    if (!m_dBuffer || m_dBufferSize < needed) {
        if (m_dBuffer) cudaFree(m_dBuffer);
        const size_t alignedPitch = ((static_cast<size_t>(w) * kRGB + 3) / 4) * 4;
        if (cudaMalloc(&m_dBuffer, alignedPitch * h) != cudaSuccess) {
            qCWarning(lcClient) << "NvJpegDecoder: cudaMalloc failed";
            m_dBuffer = nullptr;
            m_dBufferSize = 0;
            return false;
        }
        m_dBufferSize = alignedPitch * h;
    }

    // 3. 异步解码到 GPU 显存
    nvjpegImage_t dst{};
    dst.channel[0] = m_dBuffer;
    dst.pitch[0]   = static_cast<size_t>(w) * kRGB;

    if (nvjpegDecode(m_handle, m_state, src, srcLen,
                     NVJPEG_OUTPUT_RGBI, &dst, m_stream)
        != NVJPEG_STATUS_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: nvjpegDecode failed";
        return false;
    }
    cudaStreamSynchronize(m_stream);

    // 4. GPU → CPU
    if (output.isNull() || output.width() != w || output.height() != h) {
        output = QImage(w, h, QImage::Format_RGB888);
    }
    if (cudaMemcpy(output.bits(), m_dBuffer, needed, cudaMemcpyDeviceToHost)
        != cudaSuccess) {
        qCWarning(lcClient) << "NvJpegDecoder: cudaMemcpy D2H failed";
        return false;
    }

    *outWidth  = w;
    *outHeight = h;
    return true;
}

// ── decodeToPBO() — GPU 解码 → PBO 映射内存（零 CPU 拷贝）───────────────

bool NvJpegDecoder::decodeToPBO(
    const QByteArray& jpegData,
    unsigned char* pboPtr,
    int width, int height,
    int pixelSize)
{
    Q_UNUSED(pixelSize);
    if (!ensureInitialized()) return false;

    const auto* src   = reinterpret_cast<const unsigned char*>(jpegData.constData());
    const size_t srcLen = static_cast<size_t>(jpegData.size());

    // 1. 尺寸验证
    int nComponents = 0;
    nvjpegChromaSubsampling_t subsampling = NVJPEG_CSS_444;
    int widths[4] = {}, heights[4] = {};
    if (nvjpegGetImageInfo(m_handle, src, srcLen,
                           &nComponents, &subsampling, widths, heights)
        != NVJPEG_STATUS_SUCCESS) {
        return false;
    }
    if (widths[0] != width || heights[0] != height) return false;

    // 2. 临时 GPU 缓冲
    constexpr int kRGB = 3;
    const size_t needed = static_cast<size_t>(width) * height * kRGB;
    unsigned char* tmp = nullptr;
    if (cudaMalloc(&tmp, needed) != cudaSuccess) return false;

    // 3. 解码
    nvjpegImage_t dst{};
    dst.channel[0] = tmp;
    dst.pitch[0]   = static_cast<size_t>(width) * kRGB;

    if (nvjpegDecode(m_handle, m_state, src, srcLen,
                     NVJPEG_OUTPUT_RGBI, &dst, m_stream)
        != NVJPEG_STATUS_SUCCESS) {
        cudaFree(tmp);
        return false;
    }
    cudaStreamSynchronize(m_stream);

    // 4. GPU → PBO（GPU 端 DMA，不经 CPU）
    if (cudaMemcpy(pboPtr, tmp, needed, cudaMemcpyDeviceToDevice) != cudaSuccess) {
        cudaFree(tmp);
        return false;
    }

    cudaFree(tmp);
    return true;
}

#else // !HAS_NVJPEG
// ──────────────────────────────────────────────────────────────────────────
// 无 nvJPEG SDK — 全部返回 false，isAvailable() 始终不可用
// ──────────────────────────────────────────────────────────────────────────

bool NvJpegDecoder::probeGPU() { return false; }

NvJpegDecoder::NvJpegDecoder()  { m_available = false; }
NvJpegDecoder::~NvJpegDecoder() {}
bool NvJpegDecoder::ensureInitialized() { return false; }
void NvJpegDecoder::releaseResources() {}

bool NvJpegDecoder::decode(const QByteArray&, QImage&, int*, int*) {
    return false;
}
bool NvJpegDecoder::decodeToPBO(const QByteArray&, unsigned char*, int, int, int) {
    return false;
}
#endif // HAS_NVJPEG
