#pragma once

#include "IDecoder.h"

#ifdef HAS_OPENCL
#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_MINIMUM_OPENCL_VERSION 120
#define CL_HPP_TARGET_OPENCL_VERSION 120
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl2.hpp>
#endif

class OpenCLDecoder : public IDecoder {
public:
    OpenCLDecoder();
    ~OpenCLDecoder() override;

    [[nodiscard]] bool isAvailable() const override { return m_available; }
    [[nodiscard]] bool decode(const QByteArray&, QImage&, int*, int*) override;
    [[nodiscard]] bool decodeToPBO(const QByteArray&, unsigned char*, int, int, int) override;
    [[nodiscard]] const char* name() const override { return "OpenCL"; }

private:
    bool m_available = false;

#ifdef HAS_OPENCL
    cl::Context        m_ctx;
    cl::CommandQueue   m_queue;
    cl::Kernel         m_kernel;
    cl::Program        m_program;
    cl::Buffer         m_coefBuf;
    cl::Buffer         m_outBuf;
    int                m_lastWidth = 0, m_lastHeight = 0;
    std::vector<float> m_coefHost;

    bool probeGPU();
    bool buildKernel();
    void releaseResources();
    bool huffmanDecode(const QByteArray& jpegData, int* w, int* h);
#endif
};
