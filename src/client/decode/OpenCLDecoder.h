#pragma once

#include "IDecoder.h"

#ifdef HAS_OPENCL
#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_MINIMUM_OPENCL_VERSION 120
#define CL_HPP_TARGET_OPENCL_VERSION 120
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl2.hpp>
#include <CL/cl_gl.h>
#endif

class OpenCLDecoder : public IDecoder {
public:
    OpenCLDecoder();
    ~OpenCLDecoder() override;

    [[nodiscard]] bool isAvailable() const override { return m_available; }
#ifndef QT_NO_OPENGL
    [[nodiscard]] bool decode(const QByteArray&, int*, int*, GLsync*, QImage* = nullptr) override;
#else
    [[nodiscard]] bool decode(const QByteArray&, int*, int*, QImage*) override;
#endif
    [[nodiscard]] const char* name() const override { return "OpenCL"; }

private:
    bool m_available = false;

#ifdef HAS_OPENCL
    // ── OpenCL 对象 ──
    cl::Context        m_ctx;
    cl::CommandQueue   m_queue;
    cl::Kernel         m_kernel;
    cl::Program        m_program;

    // ── GPU 缓冲区 ──
    cl::Buffer m_coefBufY, m_coefBufCb, m_coefBufCr;
    cl::Buffer m_qtblBuf;

    // ── CL/GL interop ──
    bool     m_interopAvailable = false;
    cl_mem   m_interopBuf = nullptr;  // clCreateFromGLBuffer(PBO)，CL 内核直写
    GLuint   m_lastPboId = 0;         // interop 缓冲关联的 PBO ID（变更时重建）

    // ── 主机端系数缓冲区 ──
    std::vector<short> m_coefY, m_coefCb, m_coefCr;
    unsigned short m_qtblHost[3][64];   // 量化表（自然顺序）

    // ── 图像/子采样维度 ──
    int m_lastWidth = 0, m_lastHeight = 0;
    int m_yBlocksW = 0, m_yBlocksH = 0;
    int m_cbBlocksW = 0, m_cbBlocksH = 0;
    int m_crBlocksW = 0, m_crBlocksH = 0;
    int m_cbHRatio = 1, m_cbVRatio = 1;
    int m_crHRatio = 1, m_crVRatio = 1;

    // ── 方法 ──
    bool probeGPU();
    bool buildKernel();
    [[nodiscard]] bool extractCoefficients(const QByteArray& jpegData,
                                             int* outW, int* outH);
    [[nodiscard]] bool setupInteropBuffer(GLuint pboId, int w, int h);
    void releaseResources();
#endif
};
