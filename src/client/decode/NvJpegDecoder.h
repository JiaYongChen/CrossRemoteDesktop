#pragma once

#include "IDecoder.h"

#ifdef HAS_NVJPEG
// 编译时链接：third_party 头文件 + 导入库
#include <nvjpeg.h>
#include <cuda_runtime_api.h>
#endif

/**
 * @brief nvJPEG GPU 解码器（编译时链接 nvjpeg64_12.dll + cudart64_12.dll）
 *
 * 构造时检测 CUDA 设备计算能力（CC >= 5.0 才可用）。
 * 不可用时 isAvailable() 返回 false，DecodeWorker 自动降级到 TurboJpegDecoder。
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
    bool m_available   = false;
    bool m_initialized = false;

#ifdef HAS_NVJPEG
    // ---- nvJPEG 句柄（跨帧复用）----
    nvjpegHandle_t    m_handle = nullptr;
    nvjpegJpegState_t m_state  = nullptr;
    cudaStream_t      m_stream = nullptr;

    // ---- GPU 显存缓冲（跨帧复用，仅尺寸变化时重建）----
    unsigned char* m_dBuffer     = nullptr;
    size_t         m_dBufferSize = 0;
#endif

    /// 检测 GPU 计算能力是否 >= 5.0
    static bool probeGPU();

    /// 延迟创建 nvJPEG 句柄和 CUDA 流（首次 decode 调用时）
    bool ensureInitialized();

    /// 释放所有 GPU 资源
    void releaseResources();
};
