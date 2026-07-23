#pragma once

#include "../IDecoder.h"
#include <memory>

#ifdef HAS_NVJPEG
#include <cuda_runtime_api.h>
#include <nvjpeg.h>

class TurboJpegDecoder;

/**
 * @brief nvJPEG GPU 解码器（Full API）
 *
 * 构造时分级探测最佳后端（HARDWARE→GPU_HYBRID→HYBRID），
 * 使用 Full API（nvjpegCreateEx + BufferDevice + StateAttach）
 * 减少运行时 cudaMalloc 碎片。
 *
 * - decode() : GPU 路径（mapWriteBuffer → nvjpegDecode → D2D → commitWriteBuffer），
 *             失败时内部惰性回退 TurboJpegDecoder
 */
class NvJpegDecoder : public IDecoder {
public:
    /// @brief 构造即完成 GPU 探测、后端选择、资源分配
    NvJpegDecoder();

    /// @brief 逆序释放所有 nvJPEG/CUDA 资源
    ~NvJpegDecoder() override;

    /// @brief 构造探测结果：解码器是否可用（DecodeWorker 选择解码器时调用）
    [[nodiscard]] bool isAvailable() const { return m_available; }

    // ── IDecoder 接口 ──
    [[nodiscard]] bool decode(const QByteArray&, int*, int*,
                               GLsync*, QImage* = nullptr) override;
    [[nodiscard]] const char* name() const override { return "nvJPEG"; }

private:
    // ── 后端探测 ──
    /// @brief 检查 CUDA 设备是否存在，获取 CC 版本
    /// @param[out] major 计算能力主版本号
    /// @param[out] minor 计算能力次版本号
    /// @return 设备可用返回 true
    bool probeGPU(int& major, int& minor);

    /// @brief 尝试创建指定后端的 handle + state + buffer
    /// @param backend 后端类型
    /// @param flags 创建标志（仅 HARDWARE 后端传入 NVJPEG_FLAGS_HW_DECODE_NO_PIPELINE）
    /// @return 创建成功返回 true
    bool tryBackend(nvjpegBackend_t backend, unsigned int flags);

    /// @brief 释放所有 GPU 资源（handle/state/buffer/stream/tmpBuf）
    void releaseResources();

    /// @brief GPU 解码核心路径（mapWriteBuffer → nvjpegDecode → D2D → commitWriteBuffer）
    bool decodeGpu(const QByteArray& jpegData, int* outWidth, int* outHeight,
                   GLsync* outFence);

    // ── 资源 ──
    nvjpegHandle_t       m_handle   = nullptr;  ///< nvJPEG 库句柄
    nvjpegJpegState_t    m_state    = nullptr;  ///< 解码状态
    nvjpegBufferDevice_t m_devBuf   = nullptr;  ///< 设备中间缓冲区（库管理）
    cudaStream_t         m_stream   = nullptr;  ///< CUDA 流

    unsigned char*       m_tmpBuf     = nullptr;  ///< 解码输出临时设备缓冲
    size_t               m_tmpBufSize = 0;        ///< 临时缓冲当前大小

    bool                 m_available  = false;                   ///< 解码器是否可用

    std::unique_ptr<TurboJpegDecoder> m_fallbackDecoder;  ///< GPU 失败时惰性回退
};

#else // !HAS_NVJPEG

// stub 实现：无 CUDA SDK 时编译通过，所有方法返回 false
class NvJpegDecoder : public IDecoder {
public:
    NvJpegDecoder() = default;
    ~NvJpegDecoder() override = default;

    [[nodiscard]] bool isAvailable() const { return false; }
    [[nodiscard]] bool decode(const QByteArray&, int*, int*,
                               GLsync*, QImage* = nullptr) override { return false; }
    [[nodiscard]] const char* name() const override { return "nvJPEG"; }
};

#endif // HAS_NVJPEG
