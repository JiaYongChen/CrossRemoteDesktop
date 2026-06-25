// OpenCL JPEG 解码内核 — CPU Huffman + GPU 反量化/IDCT/YCbCr→RGB
// 双内核架构：CbCr IDCT（每色度块一次）+ Y IDCT & RGB（每亮度块一次）
// 消除 4:2:0 下 75% 冗余 Cb/Cr IDCT 计算，降低寄存器压力

// ── AAN (Arai-Agui-Nakajima) 快速 8 点 1D IDCT ──
// 5 次乘法 + 29 次加法
// 参考：IJG libjpeg jidctflt.c (Thomas G. Lane, Guido Vollbeding)
//
// AAN 是"缩放 DCT"，其缩放因子是频率相关的：
//   1D 放大因子 A[k] = 2/cos(kπ/16)（k>0），A[0] = 2√2
//   2D 放大因子 A[u,v] = A[u] × A[v]
// 用频率补偿表 DEQUANT_SCALE[u*8+v] = 1 / A[u,v] 逐系数精确补偿。
//
// 常数说明（缩放至 IJG 等价形式）：
//   SQRT2     = √2                — c4 的 2 倍，用于偶部和奇部旋转
//   C2X2      = 2·cos(π/8)       — 奇部第一阶段旋转
//   C2mC6X2   = 2·(cos(π/8)−sin(π/8)) — 奇部 z12 系数
//   C2pC6X2   = 2·(cos(π/8)+sin(π/8)) — 奇部 z10 系数

__constant float SQRT2    = 1.4142135623730951f;
__constant float C2X2     = 1.8477590650225735f;
__constant float C2mC6X2  = 1.0823922002923940f;
__constant float C2pC6X2  = 2.6131259297527530f;

// ── 频率补偿表 ──
// DEQUANT_SCALE[u*8+v] = 1 / (A[u] × A[v])，矩阵对称
__constant float DEQUANT_SCALE[64] = {
    0.125000000f, 0.173379297f, 0.163320397f, 0.146985797f, 0.125000000f, 0.098211353f, 0.067649051f, 0.034487422f,
    0.173379297f, 0.240484982f, 0.226526350f, 0.203869264f, 0.173379297f, 0.136220909f, 0.093829743f, 0.047833474f,
    0.163320397f, 0.226526350f, 0.213388348f, 0.192044472f, 0.163320397f, 0.128316382f, 0.088388348f, 0.045059763f,
    0.146985797f, 0.203869264f, 0.192044472f, 0.172851548f, 0.146985797f, 0.115485173f, 0.079547411f, 0.040553701f,
    0.125000000f, 0.173379297f, 0.163320397f, 0.146985797f, 0.125000000f, 0.098211353f, 0.067649051f, 0.034487422f,
    0.098211353f, 0.136220909f, 0.128316382f, 0.115485173f, 0.098211353f, 0.077164316f, 0.053151545f, 0.027096428f,
    0.067649051f, 0.093829743f, 0.088388348f, 0.079547411f, 0.067649051f, 0.053151545f, 0.036611652f, 0.018664153f,
    0.034487422f, 0.047833474f, 0.045059763f, 0.040553701f, 0.034487422f, 0.027096428f, 0.018664153f, 0.009515008f
};

static void idct_1d(float p[8]) {
    // ── 偶部（相位 3 → 5-3 → 2）──
    float et0 = p[0] + p[4];
    float et1 = p[0] - p[4];
    float et2 = p[2] + p[6];
    float et3 = (p[2] - p[6]) * SQRT2 - et2;

    float e0 = et0 + et2;
    float e3 = et0 - et2;
    float e1 = et1 + et3;
    float e2 = et1 - et3;

    // ── 奇部（相位 6 → 5 → 2）──
    float oz13 = p[5] + p[3];
    float oz10 = p[5] - p[3];
    float oz11 = p[1] + p[7];
    float oz12 = p[1] - p[7];

    float o7 = oz11 + oz13;
    float o11 = (oz11 - oz13) * SQRT2;

    float z5 = (oz10 + oz12) * C2X2;
    float o10 = z5 - oz12 * C2mC6X2;
    float o12 = z5 - oz10 * C2pC6X2;

    float o6 = o12 - o7;
    float o5 = o11 - o6;
    float o4 = o10 - o5;

    // ── 最终输出（自然顺序 0..7）──
    p[0] = e0 + o7;
    p[1] = e1 + o6;
    p[2] = e2 + o5;
    p[3] = e3 + o4;
    p[4] = e3 - o4;
    p[5] = e2 - o5;
    p[6] = e1 - o6;
    p[7] = e0 - o7;
}

static void transpose8(float p[64]) {
    for (int i = 0; i < 8; i++)
        for (int j = i + 1; j < 8; j++) {
            float t = p[i * 8 + j];
            p[i * 8 + j] = p[j * 8 + i];
            p[j * 8 + i] = t;
        }
}

static void idct_2d(float block[64]) {
    for (int r = 0; r < 8; r++) idct_1d(&block[r * 8]);
    transpose8(block);
    for (int r = 0; r < 8; r++) idct_1d(&block[r * 8]);
    transpose8(block);
}

// ── 内核 A：Cb/Cr 反量化 + 2D IDCT ──
// work-group 覆盖所有色度块（Cb + Cr 两倍宽度）
// 左侧 half（gx < cbBlocksW）处理 Cb，右侧 half 处理 Cr
// 每 work-item 仅 1 个 float[64]，寄存器压力减半
__kernel void jpeg_decode_cbcr(
    __global const short* cb_coefs,
    __global const short* cr_coefs,
    __constant ushort* qtbl,
    __global float* cb_spatial,
    __global float* cr_spatial,
    int cbBlocksW, int cbBlocksH,
    int crBlocksW, int crBlocksH)
{
    int gx = get_global_id(0);
    int gy = get_global_id(1);

    float block[64];

    if (gx < cbBlocksW) {
        // ── Cb 块 ──
        if (gy >= cbBlocksH) return;
        int idx = (gy * cbBlocksW + gx) * 64;
        for (int i = 0; i < 64; i++)
            block[i] = (float)cb_coefs[idx + i] * (float)qtbl[1 * 64 + i] * DEQUANT_SCALE[i];
        idct_2d(block);
        for (int i = 0; i < 64; i++)
            cb_spatial[idx + i] = block[i];
    } else {
        // ── Cr 块 ──
        int cx = gx - cbBlocksW;
        if (gy >= crBlocksH) return;
        int idx = (gy * crBlocksW + cx) * 64;
        for (int i = 0; i < 64; i++)
            block[i] = (float)cr_coefs[idx + i] * (float)qtbl[2 * 64 + i] * DEQUANT_SCALE[i];
        idct_2d(block);
        for (int i = 0; i < 64; i++)
            cr_spatial[idx + i] = block[i];
    }
}

// ── 内核 B：Y 反量化 + 2D IDCT + YCbCr→RGB ──
// 读取预计算的 Cb/Cr 空间域值，无需在每 Y work-item 中重复 IDCT
__kernel void jpeg_decode_y_rgb(
    __global const short* y_coefs,
    __constant ushort* qtbl,
    __global const float* cb_spatial,
    __global const float* cr_spatial,
    __global uchar* output,
    int imgW, int imgH,
    int yBlocksW,
    int cbBlocksW, int cbBlocksH,
    int crBlocksW, int crBlocksH,
    int cbHRatio, int cbVRatio,
    int crHRatio, int crVRatio)
{
    int bx = get_global_id(0);
    int by = get_global_id(1);

    // ── Y 反量化（1 个 float[64]，寄存器压力为原来的 1/3）──
    int yBidx = (by * yBlocksW + bx) * 64;
    float yBlock[64];
    for (int i = 0; i < 64; i++)
        yBlock[i] = (float)y_coefs[yBidx + i] * (float)qtbl[0 * 64 + i] * DEQUANT_SCALE[i];

    // ── Y 2D IDCT ──
    idct_2d(yBlock);

    // ── 定位 Cb/Cr 块 ──
    int cbBidx = ((by / cbVRatio) * cbBlocksW + (bx / cbHRatio)) * 64;
    int crBidx = ((by / crVRatio) * crBlocksW + (bx / crHRatio)) * 64;

    int cbOffX = (bx % cbHRatio) * (8 / cbHRatio);
    int cbOffY = (by % cbVRatio) * (8 / cbVRatio);
    int crOffX = (bx % crHRatio) * (8 / crHRatio);
    int crOffY = (by % crVRatio) * (8 / crVRatio);

    // ── YCbCr→RGB（BT.601）+ 双线性色度上采样 ──
    for (int y = 0; y < 8; y++) {
        int py = by * 8 + y;
        if (py >= imgH) continue;

        int cy  = y / cbVRatio + cbOffY;
        int cy1 = (cy + 1 < 8) ? (cy + 1) : cy;
        float cfy = (float)(y % cbVRatio) / (float)cbVRatio;

        int cry  = y / crVRatio + crOffY;
        int cry1 = (cry + 1 < 8) ? (cry + 1) : cry;
        float crfy = (float)(y % crVRatio) / (float)crVRatio;

        for (int x = 0; x < 8; x++) {
            int px = bx * 8 + x;
            if (px >= imgW) continue;

            int cx  = x / cbHRatio + cbOffX;
            int cx1 = (cx + 1 < 8) ? (cx + 1) : cx;
            float cfx = (float)(x % cbHRatio) / (float)cbHRatio;

            int crx  = x / crHRatio + crOffX;
            int crx1 = (crx + 1 < 8) ? (crx + 1) : crx;
            float crfx = (float)(x % crHRatio) / (float)crHRatio;

            float Y_val = yBlock[y * 8 + x] + 128.0f;

            // 双线性 Cb（从预计算空间域 buffer 读取）
            float cb00 = cb_spatial[cbBidx + cy  * 8 + cx];
            float cb10 = cb_spatial[cbBidx + cy  * 8 + cx1];
            float cb01 = cb_spatial[cbBidx + cy1 * 8 + cx];
            float cb11 = cb_spatial[cbBidx + cy1 * 8 + cx1];
            float Cb_val = (cb00 * (1.0f - cfx) + cb10 * cfx) * (1.0f - cfy)
                         + (cb01 * (1.0f - cfx) + cb11 * cfx) * cfy;

            // 双线性 Cr（从预计算空间域 buffer 读取）
            float cr00 = cr_spatial[crBidx + cry  * 8 + crx];
            float cr10 = cr_spatial[crBidx + cry  * 8 + crx1];
            float cr01 = cr_spatial[crBidx + cry1 * 8 + crx];
            float cr11 = cr_spatial[crBidx + cry1 * 8 + crx1];
            float Cr_val = (cr00 * (1.0f - crfx) + cr10 * crfx) * (1.0f - crfy)
                         + (cr01 * (1.0f - crfx) + cr11 * crfx) * crfy;

            float r = Y_val + 1.402f * Cr_val;
            float g = Y_val - 0.34414f * Cb_val - 0.71414f * Cr_val;
            float b = Y_val + 1.772f * Cb_val;

            int outIdx = (py * imgW + px) * 3;
            output[outIdx + 0] = (uchar)(r < 0.0f ? 0.0f : (r > 255.0f ? 255.0f : r + 0.5f));
            output[outIdx + 1] = (uchar)(g < 0.0f ? 0.0f : (g > 255.0f ? 255.0f : g + 0.5f));
            output[outIdx + 2] = (uchar)(b < 0.0f ? 0.0f : (b > 255.0f ? 255.0f : b + 0.5f));
        }
    }
}
