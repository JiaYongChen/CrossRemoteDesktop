#include "OpenCLDecoder.h"
#include "JpegCoefficientDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <cstring>
#include <QtCore/QFile>

#include "IDecodeTarget.h"

#ifdef HAS_OPENCL

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>  // wglGetCurrentContext / wglGetCurrentDC
#endif

// ── probeGPU ──────────────────────────────────────────────────────────────

bool OpenCLDecoder::probeGPU() {
    try {
        // 1. 查找 OpenCL 设备
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);
        if (platforms.empty()) {
            qCDebug(lcClient) << "OpenCLDecoder: no platforms";
            return false;
        }

        cl::Platform platform;
        std::vector<cl::Device> devices;
        for (auto& p : platforms) {
            p.getDevices(CL_DEVICE_TYPE_GPU, &devices);
            if (!devices.empty()) { platform = p; break; }
        }
        if (devices.empty()) {
            qCDebug(lcClient) << "OpenCLDecoder: no GPU devices";
            return false;
        }

        cl::Device device = devices[0];
        std::string devName = device.getInfo<CL_DEVICE_NAME>();
        std::string devExts  = device.getInfo<CL_DEVICE_EXTENSIONS>();

        // 2. 探测 CL/GL interop 支持（必须——无 interop 时走 CPU 解码更快）
        const bool extGlSharing = (devExts.find("cl_khr_gl_sharing") != std::string::npos);
        if (!extGlSharing) {
            qCInfo(lcClient) << "OpenCLDecoder: cl_khr_gl_sharing 不支持 — 回退 CPU 解码";
            return false;
        }

    #ifdef _WIN32
        const HGLRC glCtx = wglGetCurrentContext();
        const HDC   glDC  = wglGetCurrentDC();
        if (!glCtx || !glDC) {
            qCInfo(lcClient) << "OpenCLDecoder: 无当前 GL 上下文 — 回退 CPU 解码";
            return false;
        }

        cl_context_properties props[] = {
            CL_CONTEXT_PLATFORM, (cl_context_properties)platform(),
            CL_GL_CONTEXT_KHR,   (cl_context_properties)glCtx,
            CL_WGL_HDC_KHR,     (cl_context_properties)glDC,
            0
        };
        m_ctx = cl::Context(devices, props);
    #else
        // Linux/macOS: 需要 GLX/EGL context，需要根据平台设置对应属性
        // 暂时仅 Windows 已验证 interop 可用，其他平台留后续扩展点
        qCInfo(lcClient) << "OpenCLDecoder: 非 Windows 平台 CL/GL interop 未实现 — 回退 CPU 解码";
        return false;
    #endif

        m_interopAvailable = true;
        qCInfo(lcClient) << "OpenCLDecoder: CL/GL interop 已启用 —"
                         << QString::fromStdString(devName);

        // ★ 临时 profiling：启用 CL event 计时
        m_queue = cl::CommandQueue(m_ctx, device);
        qCInfo(lcClient) << "OpenCLDecoder: device" << QString::fromStdString(devName);

        // 3. 编译内核
        if (!buildKernel()) return false;

        return true;
    } catch (const cl::Error& e) {
        qCDebug(lcClient) << "OpenCLDecoder: probe failed —" << e.what() << "(" << e.err() << ")";
        return false;
    }
}

// ── buildKernel ───────────────────────────────────────────────────────────

bool OpenCLDecoder::buildKernel() {
    QFile f(":/opencl/jpeg_decode.cl");
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcClient) << "OpenCLDecoder: kernel resource not found";
        return false;
    }
    std::string src = f.readAll().toStdString();

    cl::Program::Sources sources;
    sources.push_back({src.c_str(), src.length()});
    m_program = cl::Program(m_ctx, sources);

    try {
        cl_int err = m_program.build();
        if (err != CL_SUCCESS) {
            auto devs = m_ctx.getInfo<CL_CONTEXT_DEVICES>();
            std::string log;
            m_program.getBuildInfo(devs[0], CL_PROGRAM_BUILD_LOG, &log);
            qCWarning(lcClient) << "OpenCLDecoder: build failed —"
                                << QString::fromStdString(log);
            return false;
        }
    } catch (const cl::BuildError& e) {
        const auto& buildLogs = e.getBuildLog();
        for (const auto& entry : buildLogs) {
            qCWarning(lcClient) << "OpenCLDecoder: build failed ["
                                << QString::fromStdString(entry.first.getInfo<CL_DEVICE_NAME>())
                                << "] —" << QString::fromStdString(entry.second);
        }
        return false;
    } catch (const cl::Error& e) {
        qCWarning(lcClient) << "OpenCLDecoder: build error —" << e.what()
                            << "(" << e.err() << ")";
        return false;
    }

    m_kernel = cl::Kernel(m_program, "jpeg_decode");
    return true;
}

// ── extractCoefficients ───────────────────────────────────────────────────
// 使用自定义 Huffman 解码器，零外部依赖，替代 libjpeg 虚拟数组方案

bool OpenCLDecoder::extractCoefficients(const QByteArray& jpegData,
                                         int* outW, int* outH)
{
    JpegCoeffResult r;
    if (!jpeg_extract_coefficients(jpegData, r)) {
        qCWarning(lcClient) << "OpenCLDecoder: jpeg_extract_coefficients failed";
        return false;
    }

    *outW = r.width;
    *outH = r.height;

    // 复制维度 → 成员变量
    m_yBlocksW = r.yBlocksW;  m_yBlocksH = r.yBlocksH;
    m_cbBlocksW = r.cbBlocksW; m_cbBlocksH = r.cbBlocksH;
    m_crBlocksW = r.crBlocksW; m_crBlocksH = r.crBlocksH;
    m_cbHRatio = r.cbHRatio; m_cbVRatio = r.cbVRatio;
    m_crHRatio = r.crHRatio; m_crVRatio = r.crVRatio;

    // 复制量化表
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 64; ++i)
            m_qtblHost[c][i] = r.qtbl[c][i];

    // 转移系数所有权（零拷贝——移动 vector）
    m_coefY  = std::move(r.coefY);
    m_coefCb = std::move(r.coefCb);
    m_coefCr = std::move(r.coefCr);

    qCDebug(lcClient) << "OpenCLDecoder: GPU path — image" << r.width << "x" << r.height
                      << "nc:" << r.numComponents << "Y blocks:" << m_yBlocksW << "x" << m_yBlocksH
                      << "Mcu:" << ((r.width+15)/16) << "x" << ((r.height+15)/16);

    return true;
}

// ── setupInteropBuffer ─────────────────────────────────────────────────────

bool OpenCLDecoder::setupInteropBuffer(GLuint pboId, int w, int h) {
    // 查找已缓存的 interop 缓冲（匹配 PBO ID）
    for (int i = 0; i < kMaxInteropCache; ++i) {
        if (m_interopPboId[i] == pboId && m_interopBuf[i]) {
            return true;  // 已缓存
        }
    }

    // 找空闲槽位（优先使用空槽，否则替换最少使用的槽位 0）
    int slot = -1;
    for (int i = 0; i < kMaxInteropCache; ++i) {
        if (m_interopBuf[i] == nullptr) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        // 全部占用 → 释放第一个槽位，用于新的 PBO（PBO 双缓冲最多 2 个并发）
        clReleaseMemObject(m_interopBuf[0]);
        m_interopBuf[0] = nullptr;
        m_interopPboId[0] = 0;
        slot = 0;
    }

    cl_int err = 0;
    m_interopBuf[slot] = clCreateFromGLBuffer(
        m_ctx(), CL_MEM_WRITE_ONLY, pboId, &err);
    if (err != CL_SUCCESS) {
        qCWarning(lcClient) << "OpenCLDecoder: clCreateFromGLBuffer failed — err" << err;
        m_interopBuf[slot] = nullptr;
        return false;
    }

    m_interopPboId[slot] = pboId;
    qCDebug(lcClient) << "OpenCLDecoder: interop buffer created for PBO" << pboId
                      << "slot" << slot << "size" << (w * h * 3) << "bytes";
    return true;
}

// ── decode ────────────────────────────────────────────────────────────────

#ifndef QT_NO_OPENGL
bool OpenCLDecoder::decode(const QByteArray& jpegData,
                            int* outWidth, int* outHeight,
                            GLsync* outFence, QImage* outImage)
#else
bool OpenCLDecoder::decode(const QByteArray& jpegData,
                            int* outWidth, int* outHeight,
                            QImage* outImage)
#endif
{
    if (!m_available) return false;

    int w = 0, h = 0;
    if (!extractCoefficients(jpegData, &w, &h)) return false;

    int nYBlocks = m_yBlocksW * m_yBlocksH;
    if (nYBlocks == 0) return false;

    // Interop 必须可用——probeGPU() 已保证 m_interopAvailable
    // 确保 PBO 已分配（interop 路径不走 mapWriteBuffer，需显式触发）
    if (!m_target || !m_target->ensureBufferReady(w, h)) {
        qCWarning(lcClient) << "OpenCLDecoder: PBO ensureBufferReady failed";
        return false;
    }
    GLuint pboId = 0;
    if ((pboId = m_target->writablePboId()) == 0
        || !setupInteropBuffer(pboId, w, h))
    {
        QImage tmp(w, h, QImage::Format_RGB888);
        qCWarning(lcClient) << "OpenCLDecoder: interop PBO not ready —"
                            << "target:" << (void*)m_target
                            << "pboId:" << pboId;
        *outWidth = w;
        *outHeight = h;
        *outFence = nullptr;
        if (outImage) *outImage = tmp;
        return false;
    }

    // 按需重建 GPU 缓冲区
    if (m_lastWidth != w || m_lastHeight != h) {
        m_coefBufY  = cl::Buffer(m_ctx, CL_MEM_READ_ONLY, m_coefY.size()  * sizeof(short));
        m_coefBufCb = cl::Buffer(m_ctx, CL_MEM_READ_ONLY, m_coefCb.size() * sizeof(short));
        m_coefBufCr = cl::Buffer(m_ctx, CL_MEM_READ_ONLY, m_coefCr.size() * sizeof(short));
        m_qtblBuf   = cl::Buffer(m_ctx, CL_MEM_READ_ONLY, 3 * 64 * sizeof(unsigned short));
        m_lastWidth = w; m_lastHeight = h;
    }

    // 查找匹配 PBO ID 的 interop 缓冲
    cl_mem interopMem = nullptr;
    for (int i = 0; i < kMaxInteropCache; ++i) {
        if (m_interopPboId[i] == pboId) {
            interopMem = m_interopBuf[i];
            break;
        }
    }
    if (!interopMem) return false;  // 不应到达：setupInteropBuffer 已创建

    // Interop 管线：Acquire GL → 上传+内核+直写 PBO → Release GL → commit
    clEnqueueAcquireGLObjects(m_queue(), 1, &interopMem, 0, nullptr, nullptr);

    m_queue.enqueueWriteBuffer(m_coefBufY,  CL_FALSE, 0,
        m_coefY.size()  * sizeof(short), m_coefY.data());
    m_queue.enqueueWriteBuffer(m_coefBufCb, CL_FALSE, 0,
        m_coefCb.size() * sizeof(short), m_coefCb.data());
    m_queue.enqueueWriteBuffer(m_coefBufCr, CL_FALSE, 0,
        m_coefCr.size() * sizeof(short), m_coefCr.data());
    m_queue.enqueueWriteBuffer(m_qtblBuf,   CL_FALSE, 0,
        3 * 64 * sizeof(unsigned short), m_qtblHost);

    cl::Buffer interopWrapper(interopMem, true);
    m_kernel.setArg(0,  m_coefBufY);
    m_kernel.setArg(1,  m_coefBufCb);
    m_kernel.setArg(2,  m_coefBufCr);
    m_kernel.setArg(3,  m_qtblBuf);
    m_kernel.setArg(4,  interopWrapper);
    m_kernel.setArg(5,  w);
    m_kernel.setArg(6,  h);
    m_kernel.setArg(7,  m_yBlocksW);
    m_kernel.setArg(8,  m_cbBlocksW);
    m_kernel.setArg(9,  m_cbBlocksH);
    m_kernel.setArg(10, m_crBlocksW);
    m_kernel.setArg(11, m_crBlocksH);
    m_kernel.setArg(12, m_cbHRatio);
    m_kernel.setArg(13, m_cbVRatio);
    m_kernel.setArg(14, m_crHRatio);
    m_kernel.setArg(15, m_crVRatio);

    m_queue.enqueueNDRangeKernel(m_kernel, cl::NullRange,
        cl::NDRange(m_yBlocksW, m_yBlocksH), cl::NullRange);

    clEnqueueReleaseGLObjects(m_queue(), 1, &interopMem, 0, nullptr, nullptr);
    m_queue.finish();

    *outWidth = w;
    *outHeight = h;
    *outFence = m_target->commitFromInterop(w, h);
    return true;
}

// ── 构造/析构 ─────────────────────────────────────────────────────────────

OpenCLDecoder::OpenCLDecoder() {
    m_available = probeGPU();
    if (m_available) qCInfo(lcClient) << "OpenCLDecoder: GPU 解码已启用";
}

OpenCLDecoder::~OpenCLDecoder() { releaseResources(); }

void OpenCLDecoder::releaseResources() {
    for (int i = 0; i < kMaxInteropCache; ++i) {
        if (m_interopBuf[i]) {
            clReleaseMemObject(m_interopBuf[i]);
            m_interopBuf[i] = nullptr;
            m_interopPboId[i] = 0;
        }
    }
    m_kernel = cl::Kernel();
    m_program = cl::Program();
}

#else // !HAS_OPENCL

OpenCLDecoder::OpenCLDecoder()  { m_available = false; }
OpenCLDecoder::~OpenCLDecoder() {}
#ifndef QT_NO_OPENGL
bool OpenCLDecoder::decode(const QByteArray&, int*, int*,
                           GLsync*, QImage*) { return false; }
#else
bool OpenCLDecoder::decode(const QByteArray&, int*, int*,
                           QImage*) { return false; }
#endif

#endif
