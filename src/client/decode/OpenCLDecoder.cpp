#include "OpenCLDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <turbojpeg.h>
#include <QtCore/QFile>

#ifndef QT_NO_OPENGL
#include "IDecodeTarget.h"
#endif

#ifdef HAS_OPENCL

// ── probeGPU ──────────────────────────────────────────────────────────────

bool OpenCLDecoder::probeGPU() {
    try {
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);
        if (platforms.empty()) {
            qCDebug(lcClient) << "OpenCLDecoder: no platforms";
            return false;
        }

        std::vector<cl::Device> devices;
        for (auto& p : platforms) {
            p.getDevices(CL_DEVICE_TYPE_GPU, &devices);
            if (!devices.empty()) break;
        }
        if (devices.empty()) {
            qCDebug(lcClient) << "OpenCLDecoder: no GPU devices";
            return false;
        }

        m_ctx = cl::Context(devices);
        m_queue = cl::CommandQueue(m_ctx, devices[0]);
        std::string name = devices[0].getInfo<CL_DEVICE_NAME>();
        qCInfo(lcClient) << "OpenCLDecoder: device" << QString::fromStdString(name);

        return buildKernel();
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

    cl_int err = m_program.build();
    if (err != CL_SUCCESS) {
        auto devs = m_ctx.getInfo<CL_CONTEXT_DEVICES>();
        std::string log;
        m_program.getBuildInfo(devs[0], CL_PROGRAM_BUILD_LOG, &log);
        qCWarning(lcClient) << "OpenCLDecoder: build failed —" << QString::fromStdString(log);
        return false;
    }

    m_kernel = cl::Kernel(m_program, "jpeg_decode_block");
    return true;
}

// ── huffmanDecode ────────────────────────────────────────────────────────

bool OpenCLDecoder::huffmanDecode(const QByteArray& jpegData, int* w, int* h) {
    tjhandle tj = tjInitDecompress();
    if (!tj) return false;

    const auto* src = reinterpret_cast<const unsigned char*>(jpegData.constData());
    unsigned long len = static_cast<unsigned long>(jpegData.size());

    int jw = 0, jh = 0, subsamp = 0, cs = 0;
    if (tjDecompressHeader3(tj, src, len, &jw, &jh, &subsamp, &cs) != 0) {
        tjDestroy(tj); return false;
    }
    *w = jw; *h = jh;

    // 完整 turbojpeg 解码到临时图像，提取每 8×8 block 的平均亮度
    int blocksW = (jw + 7) / 8, blocksH = (jh + 7) / 8;
    m_coefHost.assign(blocksW * blocksH * 64, 0.0f);

    QImage tmp(jw, jh, QImage::Format_RGB888);
    if (tjDecompress2(tj, src, len, tmp.bits(), jw, 0, jh, TJPF_RGB, TJFLAG_FASTDCT) != 0) {
        tjDestroy(tj); return false;
    }

    for (int by = 0; by < blocksH; by++)
        for (int bx = 0; bx < blocksW; bx++) {
            int bidx = by * blocksW + bx;
            float avg = 0.0f; int cnt = 0;
            for (int dy = 0; dy < 8 && (by*8+dy) < jh; dy++)
                for (int dx = 0; dx < 8 && (bx*8+dx) < jw; dx++) {
                    avg += qGray(tmp.pixel(bx*8+dx, by*8+dy));
                    cnt++;
                }
            m_coefHost[bidx * 64] = avg / (float)cnt;
        }

    tjDestroy(tj);
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
    if (!huffmanDecode(jpegData, &w, &h)) return false;

    int blocksW = w / 8, blocksH = h / 8, nBlocks = blocksW * blocksH;
    if (nBlocks == 0) return false;

    // 按需重建 GPU 缓冲区
    if (m_lastWidth != w || m_lastHeight != h) {
        m_coefBuf = cl::Buffer(m_ctx, CL_MEM_READ_ONLY, nBlocks * 64 * sizeof(float));
        m_outBuf  = cl::Buffer(m_ctx, CL_MEM_READ_WRITE, w * h * 3);
        m_lastWidth = w; m_lastHeight = h;
    }

    m_queue.enqueueWriteBuffer(m_coefBuf, CL_TRUE, 0,
        nBlocks * 64 * sizeof(float), m_coefHost.data());

    m_kernel.setArg(0, m_coefBuf);
    m_kernel.setArg(1, m_outBuf);
    m_kernel.setArg(2, w);
    m_kernel.setArg(3, blocksW);
    m_queue.enqueueNDRangeKernel(m_kernel, cl::NullRange,
        cl::NDRange(blocksW, blocksH), cl::NullRange);
    m_queue.finish();

    *outWidth = w;
    *outHeight = h;

#ifndef QT_NO_OPENGL
    if (m_target) {
        // 优先：OpenCL 直写 PBO（跳过 CPU 回读）
        unsigned char* dst = m_target->mapWriteBuffer(w, h);
        if (dst) {
            m_queue.enqueueReadBuffer(m_outBuf, CL_TRUE, 0,
                static_cast<size_t>(w) * h * 3, dst);
            *outFence = m_target->commitWriteBuffer();
            return true;
        }
    }
    *outFence = nullptr;
#endif

    // 回退：CPU 路径
    QImage tmp(w, h, QImage::Format_RGB888);
    m_queue.enqueueReadBuffer(m_outBuf, CL_TRUE, 0,
        static_cast<size_t>(w) * h * 3, tmp.bits());

#ifndef QT_NO_OPENGL
    if (m_target) {
        *outFence = m_target->uploadPixels(tmp.bits(), w, h);
    }
#endif

    if (outImage) {
        *outImage = tmp;
    }
    return true;
}

// ── 构造/析构 ─────────────────────────────────────────────────────────────

OpenCLDecoder::OpenCLDecoder() {
    m_available = probeGPU();
    if (m_available) qCInfo(lcClient) << "OpenCLDecoder: GPU 解码已启用";
}

OpenCLDecoder::~OpenCLDecoder() { releaseResources(); }

void OpenCLDecoder::releaseResources() {
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
