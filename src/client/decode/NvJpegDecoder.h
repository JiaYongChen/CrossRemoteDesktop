#pragma once

#include "IDecoder.h"

#ifdef HAS_NVJPEG
#include <cuda_runtime_api.h>
#include <nvjpeg.h>

/**
 * @brief nvJPEG GPU 解码器（Full API）
 *
 * 构造时分级探测最佳后端（HARDWARE→GPU_HYBRID→HYBRID），
 * 使用 Full API（nvjpegCreateEx + BufferDevice + StateAttach）
 * 减少运行时 cudaMalloc 碎片。
 *
 * - decode()      : 始终返回 false（无 CPU 路径）
 * - decodeToPBO() : GPU 解码 → D2D 直写 PBO（核心路径）
 */
class NvJpegDecoder : public IDecoder {
public:
    /// @brief 构造即完成 GPU 探测、后端选择、资源分配
    NvJpegDecoder();

    /// @brief 逆序释放所有 nvJPEG/CUDA 资源
    ~NvJpegDecoder() override;

    // ── IDecoder 接口 ──
    [[nodiscard]] bool isAvailable() const override { return m_available; }
    [[nodiscard]] bool decode(const QByteArray&, QImage&, int*, int*) override;
    [[nodiscard]] bool decodeToPBO(const QByteArray&, unsigned char*,
                                   int, int, int) override;
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

    // ── 资源 ──
    nvjpegHandle_t       m_handle   = nullptr;  ///< nvJPEG 库句柄
    nvjpegJpegState_t    m_state    = nullptr;  ///< 解码状态
    nvjpegBufferDevice_t m_devBuf   = nullptr;  ///< 设备中间缓冲区（库管理）
    cudaStream_t         m_stream   = nullptr;  ///< CUDA 流

    unsigned char*       m_tmpBuf     = nullptr;  ///< 解码输出临时设备缓冲
    size_t               m_tmpBufSize = 0;        ///< 临时缓冲当前大小

    nvjpegBackend_t      m_backend    = NVJPEG_BACKEND_DEFAULT;  ///< 选定的后端
    bool                 m_available  = false;                   ///< 解码器是否可用
};

#else // !HAS_NVJPEG

// stub 实现：无 CUDA SDK 时编译通过，所有方法返回 false
class NvJpegDecoder : public IDecoder {
public:
    NvJpegDecoder() = default;
    ~NvJpegDecoder() override = default;

    [[nodiscard]] bool isAvailable() const override { return false; }
    [[nodiscard]] bool decode(const QByteArray&, QImage&, int*, int*) override { return false; }
    [[nodiscard]] bool decodeToPBO(const QByteArray&, unsigned char*,
                                   int, int, int) override { return false; }
    [[nodiscard]] const char* name() const override { return "nvJPEG"; }
};

#endif // HAS_NVJPEG
