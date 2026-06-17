#pragma once

#include "IDecoder.h"

#ifdef HAS_NVJPEG
#include <QtCore/QLibrary>

// ═══════════════════════════════════════════════════════════════════════════
// nvJPEG 最小声明（替代 NVIDIA nvjpeg.h，避免 80+ 头文件依赖链）
// 函数由 nvjpeg.lib 导入，Delay-Load nvjpeg64_12.dll
// ═══════════════════════════════════════════════════════════════════════════

struct NvJpegLib;     using nvjpegHandle_t    = NvJpegLib*;
struct NvJpegState;   using nvjpegJpegState_t = NvJpegState*;

struct nvjpegImage_t {
    unsigned char* channel[4];
    size_t         pitch[4];
};

enum nvjpegStatus_t           { NVJPEG_STATUS_SUCCESS = 0 };
enum nvjpegOutputFormat_t     { NVJPEG_OUTPUT_RGBI = 5 };
enum nvjpegChromaSubsampling_t { NVJPEG_CSS_444 = 0 };

extern "C" {
    __declspec(dllimport) int nvjpegCreateSimple(nvjpegHandle_t*);
    __declspec(dllimport) int nvjpegJpegStateCreate(nvjpegHandle_t, nvjpegJpegState_t*);
    __declspec(dllimport) int nvjpegJpegStateDestroy(nvjpegJpegState_t);
    __declspec(dllimport) int nvjpegGetImageInfo(nvjpegHandle_t, const unsigned char*,
        size_t, int*, nvjpegChromaSubsampling_t*, int*, int*);
    __declspec(dllimport) int nvjpegDecode(nvjpegHandle_t, nvjpegJpegState_t,
        const unsigned char*, size_t, nvjpegOutputFormat_t, nvjpegImage_t*, void*);
    __declspec(dllimport) int nvjpegDestroy(nvjpegHandle_t);
}
#endif // HAS_NVJPEG


/**
 * @brief nvJPEG GPU 解码器
 *
 * - nvJPEG 函数: 编译时链接 nvjpeg.lib，Delay-Load nvjpeg64_12.dll
 * - CUDA 函数:   运行时 QLibrary 加载 cudart64_12.dll（NVIDIA 驱动提供）
 * - probeGPU():  先通过 QLibrary 检查 CUDA 设备，CC >= 5.0 才可用
 */
class NvJpegDecoder : public IDecoder {
public:
    NvJpegDecoder();
    ~NvJpegDecoder() override;

    [[nodiscard]] bool isAvailable() const override { return m_available; }
    [[nodiscard]] bool decode(const QByteArray&, QImage&, int*, int*) override;
    [[nodiscard]] bool decodeToPBO(const QByteArray&, unsigned char*, int, int, int) override;
    [[nodiscard]] const char* name() const override { return "nvJPEG"; }

private:
    bool m_available   = false;
    bool m_initialized = false;

#ifdef HAS_NVJPEG
    QLibrary m_cudaLib;

    // CUDA 函数指针（QLibrary 运行时加载）
    using Fn_GetDeviceCount = int(*)(int*);
    using Fn_GetDevice      = int(*)(int*);
    using Fn_DeviceGetAttr  = int(*)(int*, int, int);
    using Fn_Malloc         = int(*)(void**, size_t);
    using Fn_Free           = int(*)(void*);
    using Fn_Memcpy         = int(*)(void*, const void*, size_t, int);
    using Fn_StreamCreate   = int(*)(void**);
    using Fn_StreamDestroy  = int(*)(void*);
    using Fn_StreamSync     = int(*)(void*);

    Fn_GetDeviceCount fnGetDeviceCount = nullptr;
    Fn_GetDevice      fnGetDevice      = nullptr;
    Fn_DeviceGetAttr  fnDeviceGetAttr  = nullptr;
    Fn_Malloc         fnMalloc         = nullptr;
    Fn_Free           fnFree           = nullptr;
    Fn_Memcpy         fnMemcpy         = nullptr;
    Fn_StreamCreate   fnStreamCreate   = nullptr;
    Fn_StreamDestroy  fnStreamDestroy  = nullptr;
    Fn_StreamSync     fnStreamSync     = nullptr;

    nvjpegHandle_t    m_handle = nullptr;
    nvjpegJpegState_t m_state  = nullptr;
    void*             m_stream = nullptr;
    unsigned char*    m_dBuffer = nullptr;
    size_t            m_dBufferSize = 0;
#endif

    bool probeGPU();
    bool ensureInitialized();
    void releaseResources();
};
