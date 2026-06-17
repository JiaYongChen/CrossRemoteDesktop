#pragma once

#include "IDecoder.h"

#ifdef HAS_NVJPEG
#include <cuda_runtime_api.h>
#include <nvjpeg.h>


/**
 * @brief nvJPEG GPU 解码器
 *
 * - nvJPEG 函数: 编译时链接 nvjpeg.lib
 * - CUDA 函数:   编译时链接 cudart.lib（cudart64_12.dll 由 GPU 驱动提供）
 * - probeGPU():  直接调用 CUDA Runtime API 检查设备，CC >= 5.0 才可用
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

    nvjpegHandle_t    m_handle = nullptr;
    nvjpegJpegState_t m_state  = nullptr;
    void*             m_stream = nullptr;
    unsigned char*    m_dBuffer = nullptr;
    size_t            m_dBufferSize = 0;

    bool probeGPU();
    bool ensureInitialized();
    void releaseResources();
};

#endif // HAS_NVJPEG
