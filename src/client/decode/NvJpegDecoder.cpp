#include "NvJpegDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"

#ifdef HAS_NVJPEG

// ── probeGPU: 直接调用 CUDA Runtime API 检查设备 ───────────────────────────

bool NvJpegDecoder::probeGPU() {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) {
        qCDebug(lcClient) << "NvJpegDecoder: no CUDA devices";
        return false;
    }

    int device = 0;
    cudaGetDevice(&device);
    int major = 0, minor = 0;
    cudaDeviceGetAttribute(&major, cudaDevAttrComputeCapabilityMajor, device);
    cudaDeviceGetAttribute(&minor, cudaDevAttrComputeCapabilityMinor, device);

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

    if (nvjpegCreateSimple(&m_handle) != NVJPEG_STATUS_SUCCESS) {
        qCWarning(lcClient) << "NvJpegDecoder: nvjpegCreateSimple failed";
        return false;
    }
    if (nvjpegJpegStateCreate(m_handle, &m_state) != NVJPEG_STATUS_SUCCESS) {
        nvjpegDestroy(m_handle); m_handle = nullptr;
        return false;
    }
    if (cudaStreamCreate(reinterpret_cast<cudaStream_t*>(&m_stream)) != cudaSuccess) {
        nvjpegJpegStateDestroy(m_state); m_state = nullptr;
        nvjpegDestroy(m_handle); m_handle = nullptr;
        return false;
    }
    m_initialized = true;
    return true;
}

void NvJpegDecoder::releaseResources() {
    if (m_stream)  { cudaStreamDestroy(static_cast<cudaStream_t>(m_stream)); m_stream = nullptr; }
    if (m_state)   { nvjpegJpegStateDestroy(m_state); m_state = nullptr; }
    if (m_handle)  { nvjpegDestroy(m_handle); m_handle = nullptr; }
    if (m_dBuffer) { cudaFree(m_dBuffer); m_dBuffer = nullptr; m_dBufferSize = 0; }
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
        if (m_dBuffer) cudaFree(m_dBuffer);
        const size_t ap = ((static_cast<size_t>(w) * kRGB + 3) / 4) * 4;
        if (cudaMalloc(reinterpret_cast<void**>(&m_dBuffer), ap * h) != cudaSuccess) {
            m_dBuffer = nullptr; m_dBufferSize = 0; return false;
        }
        m_dBufferSize = ap * h;
    }

    nvjpegImage_t dst{};
    dst.channel[0] = m_dBuffer;
    dst.pitch[0]   = static_cast<size_t>(w) * kRGB;

    if (nvjpegDecode(m_handle, m_state, src, len, NVJPEG_OUTPUT_RGBI, &dst,
                     static_cast<cudaStream_t>(m_stream)) != 0)
        return false;
    cudaStreamSynchronize(static_cast<cudaStream_t>(m_stream));

    if (output.isNull() || output.width() != w || output.height() != h)
        output = QImage(w, h, QImage::Format_RGB888);
    cudaMemcpy(output.bits(), m_dBuffer, need, cudaMemcpyDeviceToHost);

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
    if (cudaMalloc(reinterpret_cast<void**>(&tmp), need) != cudaSuccess) return false;

    nvjpegImage_t dst{};
    dst.channel[0] = tmp;
    dst.pitch[0]   = static_cast<size_t>(width) * kRGB;

    if (nvjpegDecode(m_handle, m_state, src, len, NVJPEG_OUTPUT_RGBI, &dst,
                     static_cast<cudaStream_t>(m_stream)) != 0) {
        cudaFree(tmp); return false;
    }
    cudaStreamSynchronize(static_cast<cudaStream_t>(m_stream));
    cudaMemcpy(pboPtr, tmp, need, cudaMemcpyDeviceToDevice);
    cudaFree(tmp);
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
