#include "NvJpegDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"

#ifdef HAS_NVJPEG
#include "TurboJpegDecoder.h"
#include "IDecodeTarget.h"

// ═══════════════════════════════════════════════════════════════════════════════
// 后端名称映射（诊断用）
// ═══════════════════════════════════════════════════════════════════════════════

static const char* backendName(nvjpegBackend_t b) {
    switch (b) {
    case NVJPEG_BACKEND_HARDWARE:   return "HARDWARE";
    case NVJPEG_BACKEND_GPU_HYBRID: return "GPU_HYBRID";
    case NVJPEG_BACKEND_HYBRID:     return "HYBRID";
    default:                        return "DEFAULT";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// probeGPU — 检查 CUDA 设备 + 获取 CC 版本
// ═══════════════════════════════════════════════════════════════════════════════

bool NvJpegDecoder::probeGPU(int& major, int& minor) {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) {
        qCDebug(lcClient) << "NvJpegDecoder: no CUDA-capable devices found";
        return false;
    }

    int device = 0;
    cudaGetDevice(&device);
    cudaDeviceGetAttribute(&major, cudaDevAttrComputeCapabilityMajor, device);
    cudaDeviceGetAttribute(&minor, cudaDevAttrComputeCapabilityMinor, device);

    // 获取 GPU 名称用于诊断
    cudaDeviceProp prop{};
    cudaGetDeviceProperties(&prop, device);
    qCInfo(lcClient) << "NvJpegDecoder: GPU =" << prop.name
                      << "CC =" << major << "." << minor;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// tryBackend — 尝试创建指定后端的完整资源栈
// ═══════════════════════════════════════════════════════════════════════════════

bool NvJpegDecoder::tryBackend(nvjpegBackend_t backend, unsigned int flags) {
    nvjpegHandle_t       h  = nullptr;
    nvjpegJpegState_t    s  = nullptr;
    nvjpegBufferDevice_t db = nullptr;
    cudaStream_t         st = nullptr;

    // 1. 创建库句柄
    nvjpegStatus_t ret = nvjpegCreateEx(backend,
        /*dev_allocator=*/nullptr,
        /*pinned_allocator=*/nullptr,
        flags, &h);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        qCDebug(lcClient) << "NvJpegDecoder: backend" << backendName(backend)
                           << "— nvjpegCreateEx failed, code:" << int(ret);
        return false;
    }

    // 2. HARDWARE 后端额外校验：确认硬件引擎可用
    // nvjpegGetHardwareDecoderInfo 在 CUDA 12.x 中引入
    if (backend == NVJPEG_BACKEND_HARDWARE) {
#if NVJPEG_VER_MAJOR >= 12
        unsigned int engines = 0, cores = 0;
        nvjpegGetHardwareDecoderInfo(h, &engines, &cores);
        if (engines == 0) {
            qCDebug(lcClient) << "NvJpegDecoder: HARDWARE backend — no decode engines";
            nvjpegDestroy(h);
            return false;
        }
        qCInfo(lcClient) << "NvJpegDecoder: HARDWARE decoder —"
                          << engines << "engine(s)," << cores << "core(s)/engine";
#else
        qCInfo(lcClient) << "NvJpegDecoder: HARDWARE backend — engine info unavailable (CUDA 11.x)";
#endif
    }

    // 3. 设置内存填充（减少内部分配碎片）
    nvjpegSetDeviceMemoryPadding(256, h);

    // 4. 创建解码状态
    ret = nvjpegJpegStateCreate(h, &s);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        qCDebug(lcClient) << "NvJpegDecoder: backend" << backendName(backend)
                           << "— nvjpegJpegStateCreate failed, code:" << int(ret);
        nvjpegDestroy(h);
        return false;
    }

    // 5. 创建 CUDA 流
    if (cudaStreamCreate(&st) != cudaSuccess) {
        qCDebug(lcClient) << "NvJpegDecoder: backend" << backendName(backend)
                           << "— cudaStreamCreate failed";
        nvjpegJpegStateDestroy(s);
        nvjpegDestroy(h);
        return false;
    }

    // 6. 创建设备缓冲区并绑定到解码状态
    ret = nvjpegBufferDeviceCreate(h, /*device_allocator=*/nullptr, &db);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        qCDebug(lcClient) << "NvJpegDecoder: backend" << backendName(backend)
                           << "— nvjpegBufferDeviceCreate failed, code:" << int(ret);
        cudaStreamDestroy(st);
        nvjpegJpegStateDestroy(s);
        nvjpegDestroy(h);
        return false;
    }

    ret = nvjpegStateAttachDeviceBuffer(s, db);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        qCDebug(lcClient) << "NvJpegDecoder: backend" << backendName(backend)
                           << "— nvjpegStateAttachDeviceBuffer failed, code:" << int(ret);
        nvjpegBufferDeviceDestroy(db);
        cudaStreamDestroy(st);
        nvjpegJpegStateDestroy(s);
        nvjpegDestroy(h);
        return false;
    }

    // 7. 全部成功 → 交由成员变量持有
    m_handle  = h;
    m_state   = s;
    m_devBuf  = db;
    m_stream  = st;
    m_backend = backend;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// releaseResources — 逆序释放所有 GPU 资源
// ═══════════════════════════════════════════════════════════════════════════════

void NvJpegDecoder::releaseResources() {
    // 解绑设备缓冲区（必须在 destroy buffer 之前）
    if (m_state && m_devBuf) {
        nvjpegStateAttachDeviceBuffer(m_state, nullptr);
    }
    if (m_devBuf)  { nvjpegBufferDeviceDestroy(m_devBuf); m_devBuf = nullptr; }
    if (m_stream)  { cudaStreamDestroy(m_stream); m_stream = nullptr; }
    if (m_tmpBuf)  { cudaFree(m_tmpBuf); m_tmpBuf = nullptr; m_tmpBufSize = 0; }
    if (m_state)   { nvjpegJpegStateDestroy(m_state); m_state = nullptr; }
    if (m_handle)  { nvjpegDestroy(m_handle); m_handle = nullptr; }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════════════════════

NvJpegDecoder::NvJpegDecoder() {
    int major = 0, minor = 0;
    if (!probeGPU(major, minor)) {
        m_available = false;
        return;
    }

    // 分级探测：HARDWARE → GPU_HYBRID → HYBRID
    if (major >= 8) {
        if (tryBackend(NVJPEG_BACKEND_HARDWARE,
                       NVJPEG_FLAGS_HW_DECODE_NO_PIPELINE)) {
            m_available = true;
            qCInfo(lcClient) << "NvJpegDecoder: selected HARDWARE backend — nvJPEG GPU 解码已启用";
            return;
        }
    }

    if (major >= 5) {
        if (tryBackend(NVJPEG_BACKEND_GPU_HYBRID, /*flags=*/0)) {
            m_available = true;
            qCInfo(lcClient) << "NvJpegDecoder: selected GPU_HYBRID backend — nvJPEG GPU 解码已启用";
            return;
        }
    }

    if (tryBackend(NVJPEG_BACKEND_HYBRID, /*flags=*/0)) {
        m_available = true;
        qCInfo(lcClient) << "NvJpegDecoder: selected HYBRID backend — nvJPEG 解码已启用";
        return;
    }

    // 所有后端均失败
    m_available = false;
    qCInfo(lcClient) << "NvJpegDecoder: no usable backend — nvJPEG 不可用";
}

NvJpegDecoder::~NvJpegDecoder() {
    releaseResources();
}

// ═══════════════════════════════════════════════════════════════════════════════
// decode — GPU 路径优先，失败回退 TurboJpegDecoder
// ═══════════════════════════════════════════════════════════════════════════════

bool NvJpegDecoder::decode(const QByteArray& jpegData,
                            int* outWidth, int* outHeight,
                            GLsync* outFence, QImage* outImage) {
    Q_UNUSED(outImage);

    // 1. 尝试 GPU 路径
    GLsync fence = nullptr;
    int w = 0, h = 0;
    if (m_available && m_target && decodeGpu(jpegData, &w, &h, &fence)) {
        *outWidth = w;
        *outHeight = h;
        *outFence = fence;
        return true;
    }

    // 2. 回退：惰性构造 TurboJpegDecoder
    if (!m_fallbackDecoder) {
        m_fallbackDecoder = std::make_unique<TurboJpegDecoder>();
    }
    if (m_target) {
        m_fallbackDecoder->setDecodeTarget(m_target);
    }

    return m_fallbackDecoder->decode(jpegData, outWidth, outHeight, outFence, outImage);
}

// ═══════════════════════════════════════════════════════════════════════════════
// decodeGpu — GPU 解码核心路径（mapWriteBuffer → nvjpegDecode → D2D → commitWriteBuffer）
// ═══════════════════════════════════════════════════════════════════════════════

bool NvJpegDecoder::decodeGpu(const QByteArray& jpegData,
                               int* outWidth, int* outHeight,
                               GLsync* outFence) {
    const auto* src = reinterpret_cast<const unsigned char*>(jpegData.constData());
    const size_t len = static_cast<size_t>(jpegData.size());

    // 1. 解析 JPEG 头
    int nComp = 0, wArr[4] = {}, hArr[4] = {};
    nvjpegChromaSubsampling_t sub{};
    if (nvjpegGetImageInfo(m_handle, src, len, &nComp, &sub, wArr, hArr)
        != NVJPEG_STATUS_SUCCESS) {
        return false;
    }

    const int w = wArr[0], h = hArr[0];

    // 2. 通过 target 获取 PBO 映射指针
    unsigned char* dst = m_target->mapWriteBuffer(w, h);
    if (!dst) return false;

    // 3. 确保临时设备缓冲区
    constexpr int kRGB = 3;
    const size_t need = static_cast<size_t>(w) * h * kRGB;
    if (!m_tmpBuf || m_tmpBufSize < need) {
        if (m_tmpBuf) cudaFree(m_tmpBuf);
        if (cudaMalloc(reinterpret_cast<void**>(&m_tmpBuf), need) != cudaSuccess) {
            m_tmpBuf = nullptr;
            m_tmpBufSize = 0;
            return false;
        }
        m_tmpBufSize = need;
    }

    // 4. nvJPEG 异步 GPU 解码
    nvjpegImage_t imgDst{};
    imgDst.channel[0] = m_tmpBuf;
    imgDst.pitch[0]   = static_cast<size_t>(w) * kRGB;
    if (nvjpegDecode(m_handle, m_state, src, len,
                     NVJPEG_OUTPUT_RGBI, &imgDst, m_stream) != NVJPEG_STATUS_SUCCESS) {
        return false;
    }
    cudaStreamSynchronize(m_stream);

    // 5. D2D 拷贝到 PBO
    if (cudaMemcpy(dst, m_tmpBuf, need, cudaMemcpyDeviceToDevice) != cudaSuccess) {
        return false;
    }

    // 6. 提交 PBO 写入
    *outFence = m_target->commitWriteBuffer();
    *outWidth = w;
    *outHeight = h;
    return true;
}

#else // !HAS_NVJPEG

// ── stub 实现（无 CUDA SDK 时编译通过）────────────────────────────────────────

bool NvJpegDecoder::decode(const QByteArray&, int*, int*,
                           GLsync*, QImage*) { return false; }

#endif // HAS_NVJPEG
