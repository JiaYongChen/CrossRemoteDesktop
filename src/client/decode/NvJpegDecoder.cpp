#include "NvJpegDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"

#ifdef HAS_NVJPEG

static constexpr int CUDA_SUCCESS = 0;
static constexpr int CUDA_MEMCPY_DEVICE_TO_HOST  = 2;
static constexpr int CUDA_MEMCPY_DEVICE_TO_DEVICE = 3;
static constexpr int CU_ATTR_CC_MAJOR = 75;
static constexpr int CU_ATTR_CC_MINOR = 76;

// ── probeGPU: QLibrary 加载 CUDA，检查设备 ───────────────────────────────

bool NvJpegDecoder::probeGPU() {
    // 加载 cudart64_12.dll（由 NVIDIA 驱动安装在 System32）
    m_cudaLib.setFileName("cudart64_12");
    if (!m_cudaLib.load()) {
        qCDebug(lcClient) << "NvJpegDecoder: cudart64_12.dll not found";
        return false;
    }

    fnGetDeviceCount = reinterpret_cast<Fn_GetDeviceCount>(m_cudaLib.resolve("cudaGetDeviceCount"));
    fnGetDevice      = reinterpret_cast<Fn_GetDevice>(m_cudaLib.resolve("cudaGetDevice"));
    fnDeviceGetAttr  = reinterpret_cast<Fn_DeviceGetAttr>(m_cudaLib.resolve("cudaDeviceGetAttribute"));
    fnMalloc         = reinterpret_cast<Fn_Malloc>(m_cudaLib.resolve("cudaMalloc"));
    fnFree           = reinterpret_cast<Fn_Free>(m_cudaLib.resolve("cudaFree"));
    fnMemcpy         = reinterpret_cast<Fn_Memcpy>(m_cudaLib.resolve("cudaMemcpy"));
    fnStreamCreate   = reinterpret_cast<Fn_StreamCreate>(m_cudaLib.resolve("cudaStreamCreate"));
    fnStreamDestroy  = reinterpret_cast<Fn_StreamDestroy>(m_cudaLib.resolve("cudaStreamDestroy"));
    fnStreamSync     = reinterpret_cast<Fn_StreamSync>(m_cudaLib.resolve("cudaStreamSynchronize"));

    if (!fnGetDeviceCount) {
        qCDebug(lcClient) << "NvJpegDecoder: CUDA symbols not resolved";
        return false;
    }

    int count = 0;
    if (fnGetDeviceCount(&count) != 0 || count == 0) {
        qCDebug(lcClient) << "NvJpegDecoder: no CUDA devices";
        return false;
    }

    int device = 0;
    fnGetDevice(&device);
    int major = 0, minor = 0;
    fnDeviceGetAttr(&major, CU_ATTR_CC_MAJOR, device);
    fnDeviceGetAttr(&minor, CU_ATTR_CC_MINOR, device);

    if (major < 5) {
        qCDebug(lcClient) << "NvJpegDecoder: GPU CC" << major << "." << minor
                          << "too old (>= 5.0 required)";
        return false;
    }

    qCInfo(lcClient) << "NvJpegDecoder: GPU CC" << major << "." << minor << "— nvJPEG ready";
    return true;
}

// ── 构造 / 析构 ──────────────────────────────────────────────────────────

NvJpegDecoder::NvJpegDecoder() {
    m_available = probeGPU();
    if (m_available) qCInfo(lcClient) << "NvJpegDecoder: nvJPEG GPU 解码已启用";
}

NvJpegDecoder::~NvJpegDecoder() { releaseResources(); }

// ── ensureInitialized ────────────────────────────────────────────────────

bool NvJpegDecoder::ensureInitialized() {
    if (m_initialized) return true;

    // 此时首次调用 nvJPEG 函数 → Delay-Load 触发 nvjpeg64_12.dll 加载
    if (nvjpegCreateSimple(&m_handle) != NVJPEG_STATUS_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: nvjpegCreateSimple failed";
        return false;
    }
    if (nvjpegJpegStateCreate(m_handle, &m_state) != NVJPEG_STATUS_SUCCESS) {
        nvjpegDestroy(m_handle); m_handle = nullptr;
        return false;
    }
    if (fnStreamCreate(&m_stream) != 0) {
        nvjpegJpegStateDestroy(m_state); m_state = nullptr;
        nvjpegDestroy(m_handle); m_handle = nullptr;
        return false;
    }
    m_initialized = true;
    return true;
}

void NvJpegDecoder::releaseResources() {
    if (m_stream)  { fnStreamDestroy(m_stream);  m_stream = nullptr; }
    if (m_state)   { nvjpegJpegStateDestroy(m_state); m_state = nullptr; }
    if (m_handle)  { nvjpegDestroy(m_handle); m_handle = nullptr; }
    if (m_dBuffer) { fnFree(m_dBuffer); m_dBuffer = nullptr; m_dBufferSize = 0; }
    m_initialized = false;
}

// ── decode ────────────────────────────────────────────────────────────────

bool NvJpegDecoder::decode(const QByteArray& jpegData, QImage& output,
                           int* outWidth, int* outHeight) {
    if (!ensureInitialized()) return false;

    const auto* src = reinterpret_cast<const unsigned char*>(jpegData.constData());
    const size_t len = static_cast<size_t>(jpegData.size());

    int nComp = 0, wArr[4] = {}, hArr[4] = {};
    nvjpegChromaSubsampling_t sub{};
    if (nvjpegGetImageInfo(m_handle, src, len, &nComp, &sub, wArr, hArr) != 0)
        return false;
    int w = wArr[0], h = hArr[0];
    if (w <= 0 || h <= 0) return false;

    constexpr int kRGB = 3;
    const size_t need = static_cast<size_t>(w) * h * kRGB;
    if (!m_dBuffer || m_dBufferSize < need) {
        if (m_dBuffer) fnFree(m_dBuffer);
        const size_t ap = ((static_cast<size_t>(w) * kRGB + 3) / 4) * 4;
        if (fnMalloc(reinterpret_cast<void**>(&m_dBuffer), ap * h) != 0) {
            m_dBuffer = nullptr; m_dBufferSize = 0; return false;
        }
        m_dBufferSize = ap * h;
    }

    nvjpegImage_t dst{};
    dst.channel[0] = m_dBuffer;
    dst.pitch[0]   = static_cast<size_t>(w) * kRGB;

    if (nvjpegDecode(m_handle, m_state, src, len, NVJPEG_OUTPUT_RGBI, &dst, m_stream) != 0)
        return false;
    fnStreamSync(m_stream);

    if (output.isNull() || output.width() != w || output.height() != h)
        output = QImage(w, h, QImage::Format_RGB888);
    fnMemcpy(output.bits(), m_dBuffer, need, CUDA_MEMCPY_DEVICE_TO_HOST);

    *outWidth = w; *outHeight = h;
    return true;
}

// ── decodeToPBO ───────────────────────────────────────────────────────────

bool NvJpegDecoder::decodeToPBO(const QByteArray& jpegData, unsigned char* pboPtr,
                                int width, int height, int pixelSize) {
    Q_UNUSED(pixelSize);
    if (!ensureInitialized()) return false;

    const auto* src = reinterpret_cast<const unsigned char*>(jpegData.constData());
    const size_t len = static_cast<size_t>(jpegData.size());

    int nComp = 0, wArr[4] = {}, hArr[4] = {};
    nvjpegChromaSubsampling_t sub{};
    if (nvjpegGetImageInfo(m_handle, src, len, &nComp, &sub, wArr, hArr) != 0)
        return false;
    if (wArr[0] != width || hArr[0] != height) return false;

    constexpr int kRGB = 3;
    const size_t need = static_cast<size_t>(width) * height * kRGB;
    unsigned char* tmp = nullptr;
    if (fnMalloc(reinterpret_cast<void**>(&tmp), need) != 0) return false;

    nvjpegImage_t dst{};
    dst.channel[0] = tmp;
    dst.pitch[0]   = static_cast<size_t>(width) * kRGB;

    if (nvjpegDecode(m_handle, m_state, src, len, NVJPEG_OUTPUT_RGBI, &dst, m_stream) != 0) {
        fnFree(tmp); return false;
    }
    fnStreamSync(m_stream);
    fnMemcpy(pboPtr, tmp, need, CUDA_MEMCPY_DEVICE_TO_DEVICE);
    fnFree(tmp);
    return true;
}

#else // !HAS_NVJPEG

bool NvJpegDecoder::probeGPU() { return false; }
NvJpegDecoder::NvJpegDecoder()  { m_available = false; }
NvJpegDecoder::~NvJpegDecoder() {}
bool NvJpegDecoder::ensureInitialized() { return false; }
void NvJpegDecoder::releaseResources() {}
bool NvJpegDecoder::decode(const QByteArray&, QImage&, int*, int*) { return false; }
bool NvJpegDecoder::decodeToPBO(const QByteArray&, unsigned char*, int, int, int) { return false; }

#endif
