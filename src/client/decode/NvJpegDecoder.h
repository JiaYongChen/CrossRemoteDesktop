#pragma once

#include "IDecoder.h"
#include <QtCore/QLibrary>

// ---- nvJPEG 类型（编译期无 CUDA 头文件依赖）----

/// nvJPEG 不透明句柄（运行时通过 DLL 操作）
using NvJpegHandle   = void*;
using NvJpegJpegState = void*;
using NvJpegDecodeParams = void*;

/// nvjpegImage_t 等效结构（nvJPEG 解码输出目标）
/// channel[i] 指向 GPU 显存中的第 i 个通道数据
/// pitch[i] 为对应通道的行步长（字节）
struct NvJpegImage {
    unsigned char* channel[4];  ///< NVJPEG_MAX_COMPONENT = 4
    size_t         pitch[4];
};

/// nvjpegOutputFormat_t 枚举值（来自 nvjpeg.h）
static constexpr int NVJPEG_OUTPUT_RGBI  = 5;   ///< 隔行 RGB（与 QImage::Format_RGB888 兼容）
static constexpr int NVJPEG_STATUS_SUCCESS = 0;

/// nvjpegChromaSubsampling_t（来自 nvjpeg.h，仅用于 getImageInfo 输出解析）
static constexpr int NVJPEG_CSS_444 = 0;

/// cudaMemcpyKind（来自 cuda_runtime_api.h）
static constexpr int CUDA_MEMCPY_DEVICE_TO_DEVICE = 3;

/// CUDA 成功码
static constexpr int CUDA_SUCCESS = 0;

/**
 * @brief nvJPEG GPU 解码器（运行时动态加载 nvjpeg64_12.dll + cudart64_120.dll）
 *
 * 编译期零 CUDA 依赖——所有 API 通过 QLibrary 运行时解析函数指针调用。
 * 若 DLL 不存在或 GPU CC < 5.0，isAvailable() 返回 false，
 * DecodeWorker 自动降级到 TurboJpegDecoder。
 */
class NvJpegDecoder : public IDecoder {
public:
    NvJpegDecoder();
    ~NvJpegDecoder() override;

    [[nodiscard]] bool isAvailable() const override { return m_available; }

    [[nodiscard]] bool decode(
        const QByteArray& jpegData,
        QImage& output,
        int* outWidth,
        int* outHeight) override;

    [[nodiscard]] bool decodeToPBO(
        const QByteArray& jpegData,
        unsigned char* pboPtr,
        int width, int height,
        int pixelSize) override;

    [[nodiscard]] const char* name() const override { return "nvJPEG"; }

private:
    bool m_available       = false;
    bool m_librariesLoaded = false;

    QLibrary m_nvjpegLib;
    QLibrary m_cudaLib;

    // ================ nvJPEG 函数指针 ================
    using Fn_nvjpegCreateSimple    = int(*)(NvJpegHandle*);
    using Fn_nvjpegJpegStateCreate = int(*)(NvJpegHandle, NvJpegJpegState*);
    using Fn_nvjpegJpegStateDestroy = int(*)(NvJpegJpegState);
    using Fn_nvjpegGetImageInfo    = int(*)(NvJpegHandle, const unsigned char*, size_t,
                                             int*, int*, int*, int*);
    using Fn_nvjpegDecode          = int(*)(NvJpegHandle, NvJpegJpegState,
                                             const unsigned char*, size_t,
                                             int, NvJpegImage*, void*);
    using Fn_nvjpegDestroy         = int(*)(NvJpegHandle);

    Fn_nvjpegCreateSimple    fnCreateSimple    = nullptr;
    Fn_nvjpegJpegStateCreate fnJpegStateCreate = nullptr;
    Fn_nvjpegJpegStateDestroy fnJpegStateDestroy = nullptr;
    Fn_nvjpegGetImageInfo    fnGetImageInfo    = nullptr;
    Fn_nvjpegDecode          fnDecode          = nullptr;
    Fn_nvjpegDestroy         fnDestroy         = nullptr;

    // ================ CUDA 函数指针 ================
    using Fn_cudaGetDeviceCount      = int(*)(int*);
    using Fn_cudaGetDevice           = int(*)(int*);
    using Fn_cudaDeviceGetAttribute  = int(*)(int*, int, int);
    using Fn_cudaMalloc              = int(*)(void**, size_t);
    using Fn_cudaFree                = int(*)(void*);
    using Fn_cudaMemcpy              = int(*)(void*, const void*, size_t, int);
    using Fn_cudaStreamCreate        = int(*)(void**);
    using Fn_cudaStreamDestroy       = int(*)(void*);
    using Fn_cudaStreamSynchronize   = int(*)(void*);

    Fn_cudaGetDeviceCount      fnGetDeviceCount = nullptr;
    Fn_cudaGetDevice           fnGetDevice      = nullptr;
    Fn_cudaDeviceGetAttribute  fnDeviceGetAttr  = nullptr;
    Fn_cudaMalloc              fnMalloc         = nullptr;
    Fn_cudaFree                fnCudaFree       = nullptr;
    Fn_cudaMemcpy              fnMemcpy         = nullptr;
    Fn_cudaStreamCreate        fnStreamCreate   = nullptr;
    Fn_cudaStreamDestroy       fnStreamDestroy  = nullptr;
    Fn_cudaStreamSynchronize   fnStreamSync     = nullptr;

    // ================ nvJPEG 句柄（复用） ================
    NvJpegHandle      m_handle = nullptr;   ///< 库句柄（跨帧复用）
    NvJpegJpegState   m_state  = nullptr;   ///< 解码状态（跨帧复用）
    void*             m_stream = nullptr;   ///< CUDA 流
    bool              m_initialized = false; ///< 句柄已创建

    /// GPU 显存缓冲区（跨帧复用，仅尺寸变化时重建）
    unsigned char* m_dBuffer     = nullptr; ///< 设备端 RGB 缓冲
    size_t         m_dBufferSize = 0;       ///< 当前缓冲大小（字节）

    // ================ 方法 ================
    bool probeAvailability();
    bool ensureInitialized();
    void releaseResources();
};
