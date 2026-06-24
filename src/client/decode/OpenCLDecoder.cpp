#include "OpenCLDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <jpeglib.h>
#include <turbojpeg.h>
#include <cstring>
#include <csetjmp>
#include <QtCore/QFile>

#ifndef QT_NO_OPENGL
#include "IDecodeTarget.h"
#endif

#ifdef HAS_OPENCL

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

        // 2. 编译内核
        if (!buildKernel()) return false;

        // 3. 能力验证：用中等尺寸 4:2:0 JPEG 测试 MCU 对齐路径
        //    小 JPEG (8×8, 4:4:4) 可能通过但真实大帧的 4:2:0 MCU 对齐会失败
        // 使用 32×24 (4:2:0): MCU=16×16, 高度不对齐 (24 % 16 ≠ 0)
        // 这会触发真实大帧中出现的 MCU 对齐问题
        QImage probeImg(32, 24, QImage::Format_RGB888);
        probeImg.fill(Qt::gray);
        tjhandle tj = tjInitCompress();
        if (!tj) {
            qCWarning(lcClient) << "OpenCLDecoder: cannot verify pipeline — tjInitCompress failed";
            return false;
        }
        unsigned char* jpegBuf = nullptr;
        unsigned long jpegSize = 0;
        int ret = tjCompress2(tj, probeImg.bits(), 32, 0, 32, TJPF_RGB,
                              &jpegBuf, &jpegSize, TJSAMP_420, 90, 0);
        tjDestroy(tj);
        if (ret != 0 || !jpegBuf) {
            qCWarning(lcClient) << "OpenCLDecoder: cannot verify pipeline — test JPEG encode failed";
            return false;
        }
        QByteArray probeJpeg(reinterpret_cast<const char*>(jpegBuf),
                             static_cast<int>(jpegSize));
        tjFree(jpegBuf);

        int probeW = 0, probeH = 0;
        if (!extractCoefficients(probeJpeg, &probeW, &probeH)) {
            qCInfo(lcClient) << "OpenCLDecoder: GPU unavailable — jpeg_read_coefficients"
                             << "not functional on this system, falling back to CPU decoder";
            return false;
        }
        qCDebug(lcClient) << "OpenCLDecoder: probe JPEG decoded OK —"
                          << probeW << "x" << probeH;
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

// ── libjpeg 错误处理：覆盖默认 exit() 行为 ──
struct JpegErrorMgr {
    jpeg_error_mgr pub;
    jmp_buf setjmpBuf;
};

static void jpegErrorExit(j_common_ptr cinfo) {
    auto* mgr = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
    longjmp(mgr->setjmpBuf, 1);
}

// ── extractCoefficients ───────────────────────────────────────────────────

bool OpenCLDecoder::extractCoefficients(const QByteArray& jpegData,
                                         int* outW, int* outH)
{
    // 1. 初始化 libjpeg 解压对象（自定义错误处理防止 exit()）
    jpeg_decompress_struct cinfo;
    JpegErrorMgr jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegErrorExit;

    if (setjmp(jerr.setjmpBuf)) {
        jpeg_destroy_decompress(&cinfo);
        qCWarning(lcClient) << "OpenCLDecoder: libjpeg fatal error during coefficient extraction";
        return false;
    }

    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo,
                 reinterpret_cast<const unsigned char*>(jpegData.constData()),
                 jpegData.size());

    // 2. 读取 JPEG 头部
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        qCWarning(lcClient) << "OpenCLDecoder: jpeg_read_header failed";
        return false;
    }

    *outW = cinfo.image_width;
    *outH = cinfo.image_height;
    int nc = cinfo.num_components;

    // 3. 提取量化表（jpeglib v6a+ 已是自然顺序）
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 64; i++)
            m_qtblHost[c][i] = 0;

    for (int c = 0; c < nc; c++) {
        int tbl_no = cinfo.comp_info[c].quant_tbl_no;
        JQUANT_TBL* tbl = cinfo.quant_tbl_ptrs[tbl_no];
        if (tbl) {
            for (int i = 0; i < 64; i++)
                m_qtblHost[c][i] = tbl->quantval[i];
        }
    }
    // 灰度 JPEG：Cb/Cr 量化表复用 Y 的（值无关，系数全为零）
    if (nc == 1) {
        for (int i = 0; i < 64; i++) {
            m_qtblHost[1][i] = m_qtblHost[0][i];
            m_qtblHost[2][i] = m_qtblHost[0][i];
        }
    }

    // 4. 读取量化 DCT 系数（先读取，再用实际维度）
    jvirt_barray_ptr* coefs = jpeg_read_coefficients(&cinfo);
    if (!coefs) {
        jpeg_destroy_decompress(&cinfo);
        qCWarning(lcClient) << "OpenCLDecoder: jpeg_read_coefficients returned null";
        return false;
    }

    // 5. 从 libjpeg 获取精确块维度（含 MCU 对齐，避免 Bogus virtual array access）
    m_yBlocksW = cinfo.comp_info[0].width_in_blocks;
    m_yBlocksH = cinfo.comp_info[0].height_in_blocks;

    qCDebug(lcClient) << "OpenCLDecoder: GPU path — image" << *outW << "x" << *outH
                      << "nc:" << nc << "Y blocks:" << m_yBlocksW << "x" << m_yBlocksH;

    if (nc >= 3) {
        m_cbHRatio = cinfo.comp_info[0].h_samp_factor / cinfo.comp_info[1].h_samp_factor;
        m_cbVRatio = cinfo.comp_info[0].v_samp_factor / cinfo.comp_info[1].v_samp_factor;
        m_crHRatio = cinfo.comp_info[0].h_samp_factor / cinfo.comp_info[2].h_samp_factor;
        m_crVRatio = cinfo.comp_info[0].v_samp_factor / cinfo.comp_info[2].v_samp_factor;

        m_cbBlocksW = cinfo.comp_info[1].width_in_blocks;
        m_cbBlocksH = cinfo.comp_info[1].height_in_blocks;
        m_crBlocksW = cinfo.comp_info[2].width_in_blocks;
        m_crBlocksH = cinfo.comp_info[2].height_in_blocks;
    } else {
        m_cbHRatio = m_cbVRatio = m_crHRatio = m_crVRatio = 1;
        m_cbBlocksW = m_cbBlocksH = m_crBlocksW = m_crBlocksH = 1;
    }

    // 5a. Y 分量
    m_coefY.resize(m_yBlocksW * m_yBlocksH * 64);
    {
        auto array = (cinfo.mem->access_virt_barray)(
            (j_common_ptr)&cinfo, coefs[0], 0, m_yBlocksH, FALSE);
        for (JDIMENSION by = 0; by < m_yBlocksH; by++) {
            JBLOCKROW row = array[by];
            for (JDIMENSION bx = 0; bx < m_yBlocksW; bx++)
                std::memcpy(&m_coefY[(by * m_yBlocksW + bx) * 64], row[bx], sizeof(JBLOCK));
        }
    }

    // 5b. Cb / Cr 分量（灰度 JPEG nc==1 时填充零）
    for (int c = 1; c <= 2; c++) {
        int bw = (c == 1) ? m_cbBlocksW : m_crBlocksW;
        int bh = (c == 1) ? m_cbBlocksH : m_crBlocksH;
        auto& buf = (c == 1) ? m_coefCb : m_coefCr;
        buf.resize(bw * bh * 64);

        if (c < nc) {
            auto array = (cinfo.mem->access_virt_barray)(
                (j_common_ptr)&cinfo, coefs[c], 0, bh, FALSE);
            for (int by = 0; by < bh; by++) {
                JBLOCKROW row = array[by];
                for (int bx = 0; bx < bw; bx++)
                    std::memcpy(&buf[(by * bw + bx) * 64], row[bx], sizeof(JBLOCK));
            }
        } else {
            std::memset(buf.data(), 0, bw * bh * 64 * sizeof(short));
        }
    }

    jpeg_destroy_decompress(&cinfo);
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

    // 按需重建 GPU 缓冲区
    if (m_lastWidth != w || m_lastHeight != h) {
        m_coefBufY  = cl::Buffer(m_ctx, CL_MEM_READ_ONLY, m_coefY.size()  * sizeof(short));
        m_coefBufCb = cl::Buffer(m_ctx, CL_MEM_READ_ONLY, m_coefCb.size() * sizeof(short));
        m_coefBufCr = cl::Buffer(m_ctx, CL_MEM_READ_ONLY, m_coefCr.size() * sizeof(short));
        m_qtblBuf   = cl::Buffer(m_ctx, CL_MEM_READ_ONLY, 3 * 64 * sizeof(unsigned short));
        m_outBuf    = cl::Buffer(m_ctx, CL_MEM_READ_WRITE, w * h * 3);
        m_lastWidth = w; m_lastHeight = h;
    }

    // 上传系数 & 量化表到 GPU
    m_queue.enqueueWriteBuffer(m_coefBufY,  CL_TRUE, 0,
        m_coefY.size()  * sizeof(short), m_coefY.data());
    m_queue.enqueueWriteBuffer(m_coefBufCb, CL_TRUE, 0,
        m_coefCb.size() * sizeof(short), m_coefCb.data());
    m_queue.enqueueWriteBuffer(m_coefBufCr, CL_TRUE, 0,
        m_coefCr.size() * sizeof(short), m_coefCr.data());
    m_queue.enqueueWriteBuffer(m_qtblBuf,   CL_TRUE, 0,
        3 * 64 * sizeof(unsigned short), m_qtblHost);

    // 设置内核参数
    m_kernel.setArg(0,  m_coefBufY);
    m_kernel.setArg(1,  m_coefBufCb);
    m_kernel.setArg(2,  m_coefBufCr);
    m_kernel.setArg(3,  m_qtblBuf);
    m_kernel.setArg(4,  m_outBuf);
    m_kernel.setArg(5,  w);
    m_kernel.setArg(6,  m_yBlocksW);
    m_kernel.setArg(7,  m_cbBlocksW);
    m_kernel.setArg(8,  m_cbBlocksH);
    m_kernel.setArg(9,  m_crBlocksW);
    m_kernel.setArg(10, m_crBlocksH);
    m_kernel.setArg(11, m_cbHRatio);
    m_kernel.setArg(12, m_cbVRatio);
    m_kernel.setArg(13, m_crHRatio);
    m_kernel.setArg(14, m_crVRatio);

    m_queue.enqueueNDRangeKernel(m_kernel, cl::NullRange,
        cl::NDRange(m_yBlocksW, m_yBlocksH), cl::NullRange);
    m_queue.finish();

    *outWidth = w;
    *outHeight = h;

#ifndef QT_NO_OPENGL
    if (m_target) {
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

    // 回退：CPU 路径（PBO 不可用时）
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
