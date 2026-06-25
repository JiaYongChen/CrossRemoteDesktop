// OpenCL JPEG 解码内核 — CPU Huffman + GPU 反量化/IDCT/YCbCr→RGB
// 每个 work-item 处理一个 8×8 Y 块
// 寄存器优化：单个 float[64] 串行用于 Y/Cb/Cr，避免寄存器溢出

// ── AAN (Arai-Agui-Nakajima) 快速 8 点 1D IDCT ──
// 5 次乘法 + 29 次加法
// 参考：IJG libjpeg jidctflt.c
//
// AAN 是"缩放 DCT"，缩放因子频率相关：
//   1D 放大 A[k] = 2/cos(kπ/16)（k>0），A[0] = 2√2
//   2D 放大 A[u,v] = A[u] × A[v]
// DEQUANT_SCALE[u*8+v] = 1/A[u,v] 进行逐系数精确补偿

__constant float SQRT2    = 1.4142135623730951f;
__constant float C2X2     = 1.8477590650225735f;
__constant float C2mC6X2  = 1.0823922002923940f;
__constant float C2pC6X2  = 2.6131259297527530f;

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
    float et0 = p[0] + p[4];
    float et1 = p[0] - p[4];
    float et2 = p[2] + p[6];
    float et3 = (p[2] - p[6]) * SQRT2 - et2;

    float e0 = et0 + et2;
    float e3 = et0 - et2;
    float e1 = et1 + et3;
    float e2 = et1 - et3;

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

// ── 主内核 ──
// 单 float[64] 数组串行处理 Y→Cb→Cr，降低寄存器压力
// Y 空间域直接用于 RGB 转换，Cb/Cr 空间域临时存入本地小数组后供上采样使用
__kernel void jpeg_decode(
    __global const short* y_coefs,
    __global const short* cb_coefs,
    __global const short* cr_coefs,
    __constant ushort* qtbl,
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

    int yBidx  = by * yBlocksW + bx;
    int cbBidx = (by / cbVRatio) * cbBlocksW + (bx / cbHRatio);
    int crBidx = (by / crVRatio) * crBlocksW + (bx / crHRatio);

    __global const short* y_src  = y_coefs  + yBidx  * 64;
    __global const short* cb_src = cb_coefs + cbBidx * 64;
    __global const short* cr_src = cr_coefs + crBidx * 64;

    // 复用单个 block 数组串行处理三个分量，消除寄存器溢出
    float block[64];

    // ── 1) Y：反量化 + 2D IDCT ──
    for (int i = 0; i < 64; i++)
        block[i] = (float)y_src[i] * (float)qtbl[0 * 64 + i] * DEQUANT_SCALE[i];
    idct_2d(block);

    // 将 Y 空间域值保存到局部数组 ySpatial（需要它做最终 RGB 转换）
    float ySpatial[64];
    for (int i = 0; i < 64; i++)
        ySpatial[i] = block[i];

    // ── 2) Cb：反量化 + 2D IDCT → cbSpatial ──
    for (int i = 0; i < 64; i++)
        block[i] = (float)cb_src[i] * (float)qtbl[1 * 64 + i] * DEQUANT_SCALE[i];
    idct_2d(block);
    float cbSpatial[64];
    for (int i = 0; i < 64; i++)
        cbSpatial[i] = block[i];

    // ── 3) Cr：反量化 + 2D IDCT → crSpatial ──
    for (int i = 0; i < 64; i++)
        block[i] = (float)cr_src[i] * (float)qtbl[2 * 64 + i] * DEQUANT_SCALE[i];
    idct_2d(block);
    float crSpatial[64];
    for (int i = 0; i < 64; i++)
        crSpatial[i] = block[i];

    // ── 4) YCbCr→RGB（BT.601）──
    int cbOffX = (bx % cbHRatio) * (8 / cbHRatio);
    int cbOffY = (by % cbVRatio) * (8 / cbVRatio);
    int crOffX = (bx % crHRatio) * (8 / crHRatio);
    int crOffY = (by % crVRatio) * (8 / crVRatio);

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

            float Y_val = ySpatial[y * 8 + x] + 128.0f;

            // 双线性 Cb
            float cb00 = cbSpatial[cy  * 8 + cx];
            float cb10 = cbSpatial[cy  * 8 + cx1];
            float cb01 = cbSpatial[cy1 * 8 + cx];
            float cb11 = cbSpatial[cy1 * 8 + cx1];
            float Cb_val = (cb00 * (1.0f - cfx) + cb10 * cfx) * (1.0f - cfy)
                         + (cb01 * (1.0f - cfx) + cb11 * cfx) * cfy;

            // 双线性 Cr
            float cr00 = crSpatial[cry  * 8 + crx];
            float cr10 = crSpatial[cry  * 8 + crx1];
            float cr01 = crSpatial[cry1 * 8 + crx];
            float cr11 = crSpatial[cry1 * 8 + crx1];
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
