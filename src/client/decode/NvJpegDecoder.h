#pragma once

#include "IDecoder.h"
#include <QtCore/QLibrary>

/**
 * @brief nvJPEG GPU 解码器（运行时动态加载）
 *
 * 通过 QLibrary 运行时解析 nvjpeg64_12.dll 和 cudart64_120.dll。
 * 编译期不依赖任何 CUDA 头文件——所有类型以裸函数指针声明。
 * 若 DLL 不存在或 GPU 不满足要求（CC < 5.0），isAvailable() 返回 false。
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
    bool m_available = false;
    QLibrary m_nvjpegLib;
    QLibrary m_cudaLib;
    bool m_librariesLoaded = false;

    // ---- CUDA 函数指针 ----
    using Fn_cudaGetDeviceCount = int(*)(int*);
    using Fn_cudaGetDevice     = int(*)(int*);
    using Fn_cudaDeviceGetAttribute = int(*)(int*, int, int);
    using Fn_cudaFree          = int(*)(void*);

    Fn_cudaGetDeviceCount     fnGetDeviceCount = nullptr;
    Fn_cudaGetDevice          fnGetDevice = nullptr;
    Fn_cudaDeviceGetAttribute fnDeviceGetAttr = nullptr;
    Fn_cudaFree               fnFree = nullptr;

    /// 探测可用性：加载 DLL → 解析符号 → 检查 GPU 计算能力
    bool probeAvailability();
};
