// OpenCL JPEG 解码内核 — CPU Huffman + GPU 反量化/IDCT/YCbCr→RGB
// 每个 work-item 处理一个 8×8 Y 块

// ── AAN (Arai-Agui-Nakajima) 快速 8 点 1D IDCT ──
// 5 次乘法 + 29 次加法（vs Chen-Wang 11 次乘法，vs 朴素矩阵 64 次乘法）
// 参考：IJG libjpeg jidctflt.c (Thomas G. Lane, Guido Vollbeding)
//       基于 Arai/Agui/Nakajima 的缩放 DCT 分解，经数十亿张图片验证
//
// 算法特性：AAN 是"缩放 DCT"——1D 变换输出 = 8 × 数学 IDCT
// 2D 分离变换输出 = 64 × 数学 2D IDCT
// 通过在反量化中预乘 1/8 = 0.125 补偿，使最终空间域值正确
//
// 常数说明（缩放至 IJG 等价形式）：
//   SQRT2     = √2                — c4 的 2 倍，用于偶部和奇部旋转
//   C2X2      = 2·cos(π/8)       — 奇部第一阶段旋转
//   C2mC6X2   = 2·(cos(π/8)−sin(π/8)) — 奇部 z12 系数
//   C2pC6X2   = 2·(cos(π/8)+sin(π/8)) — 奇部 z10 系数

__constant float SQRT2    = 1.4142135623730951f;   // √2
__constant float C2X2     = 1.8477590650225735f;   // 2 * cos(π/8)
__constant float C2mC6X2  = 1.0823922002923940f;   // 2 * (cos(π/8) - sin(π/8))
__constant float C2pC6X2  = 2.6131259297527530f;   // 2 * (cos(π/8) + sin(π/8))

static void idct_1d(float p[8]) {
    // ── 偶部（相位 3 → 5-3 → 2）──
    // 处理偶数索引系数 F0, F2, F4, F6
    float et0 = p[0] + p[4];                         // tmp10 = F0 + F4
    float et1 = p[0] - p[4];                         // tmp11 = F0 - F4
    float et2 = p[2] + p[6];                         // tmp13 = F2 + F6
    float et3 = (p[2] - p[6]) * SQRT2 - et2;         // tmp12 = (F2-F6)*√2 - (F2+F6)

    float e0 = et0 + et2;                            // tmp0 = 相位 2
    float e3 = et0 - et2;                            // tmp3
    float e1 = et1 + et3;                            // tmp1
    float e2 = et1 - et3;                            // tmp2

    // ── 奇部（相位 6 → 5 → 2）──
    // 处理奇数索引系数 F1, F3, F5, F7
    float oz13 = p[5] + p[3];                        // z13 = F5 + F3
    float oz10 = p[5] - p[3];                        // z10 = F5 - F3
    float oz11 = p[1] + p[7];                        // z11 = F1 + F7
    float oz12 = p[1] - p[7];                        // z12 = F1 - F7

    float o7 = oz11 + oz13;                          // tmp7 = z11 + z13（相位 5）
    float o11 = (oz11 - oz13) * SQRT2;               // tmp11 = (z11-z13)*√2

    float z5 = (oz10 + oz12) * C2X2;                 // z5 = (z10+z12)*2*cos(π/8)
    float o10 = z5 - oz12 * C2mC6X2;                 // tmp10 = z5 - z12*2*(c2-c6)
    float o12 = z5 - oz10 * C2pC6X2;                 // tmp12 = z5 - z10*2*(c2+c6)

    float o6 = o12 - o7;                             // tmp6 = tmp12 - tmp7（相位 2）
    float o5 = o11 - o6;                             // tmp5 = tmp11 - tmp6
    float o4 = o10 - o5;                             // tmp4 = tmp10 - tmp5

    // ── 最终输出（自然顺序 0..7）──
    // IJG 在 workspace 中以交错顺序存储，这里直接写入自然顺序
    p[0] = e0 + o7;                                  // DC-like → 空间位置 0
    p[1] = e1 + o6;                                  // 基频 → 空间位置 1
    p[2] = e2 + o5;                                  // 空间位置 2
    p[3] = e3 + o4;                                  // 空间位置 3
    p[4] = e3 - o4;                                  // 空间位置 4
    p[5] = e2 - o5;                                  // 空间位置 5
    p[6] = e1 - o6;                                  // 空间位置 6
    p[7] = e0 - o7;                                  // Nyquist → 空间位置 7
}

// ── 8×8 转置 ──
static void transpose8(float p[64]) {
    for (int i = 0; i < 8; i++)
        for (int j = i + 1; j < 8; j++) {
            float t = p[i * 8 + j];
            p[i * 8 + j] = p[j * 8 + i];
            p[j * 8 + i] = t;
        }
}

// ── 2D IDCT（行 → 转置 → 列 → 转置）──
static void idct_2d(float block[64]) {
    for (int r = 0; r < 8; r++) idct_1d(&block[r * 8]);
    transpose8(block);
    for (int r = 0; r < 8; r++) idct_1d(&block[r * 8]);
    transpose8(block);
}

// ── 主内核 ──
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

    // ── 定位各分量块 ──
    int yBidx  = by * yBlocksW + bx;
    int cbBidx = (by / cbVRatio) * cbBlocksW + (bx / cbHRatio);
    int crBidx = (by / crVRatio) * crBlocksW + (bx / crHRatio);

    __global const short* y_src  = y_coefs  + yBidx  * 64;
    __global const short* cb_src = cb_coefs + cbBidx * 64;
    __global const short* cr_src = cr_coefs + crBidx * 64;

    // ── 反量化（系数自然顺序 × 量化表自然顺序 × AAN 缩放补偿 1/8）──
    // 0.125 = 1/8：AAN 2D IDCT 输出是数学 IDCT 的 8 倍，预除 1/8 得到正确的空间域值
    float yBlock[64], cbBlock[64], crBlock[64];
    for (int i = 0; i < 64; i++) {
        yBlock[i]  = (float)y_src[i]  * (float)qtbl[0 * 64 + i] * 0.125f;
        cbBlock[i] = (float)cb_src[i] * (float)qtbl[1 * 64 + i] * 0.125f;
        crBlock[i] = (float)cr_src[i] * (float)qtbl[2 * 64 + i] * 0.125f;
    }

    // ── 2D IDCT ──
    idct_2d(yBlock);
    idct_2d(cbBlock);
    idct_2d(crBlock);

    // ── YCbCr→RGB（BT.601）+ 双线性色度上采样 ──
    // 色度偏移：处理子采样网格对齐
    int cbOffX = (bx % cbHRatio) * (8 / cbHRatio);
    int cbOffY = (by % cbVRatio) * (8 / cbVRatio);
    int crOffX = (bx % crHRatio) * (8 / crHRatio);
    int crOffY = (by % crVRatio) * (8 / crVRatio);

    for (int y = 0; y < 8; y++) {
        int py = by * 8 + y;
        int cy  = y / cbVRatio + cbOffY;
        int cy1 = (cy + 1 < 8) ? (cy + 1) : cy;
        float cfy = (float)(y % cbVRatio) / (float)cbVRatio;  // 4:2:0 → 0.0 or 0.5

        int cry  = y / crVRatio + crOffY;
        int cry1 = (cry + 1 < 8) ? (cry + 1) : cry;
        float crfy = (float)(y % crVRatio) / (float)crVRatio;

        for (int x = 0; x < 8; x++) {
            int px = bx * 8 + x;
            if (px >= imgW || py >= imgH) continue;

            int cx  = x / cbHRatio + cbOffX;
            int cx1 = (cx + 1 < 8) ? (cx + 1) : cx;
            float cfx = (float)(x % cbHRatio) / (float)cbHRatio;

            int crx  = x / crHRatio + crOffX;
            int crx1 = (crx + 1 < 8) ? (crx + 1) : crx;
            float crfx = (float)(x % crHRatio) / (float)crHRatio;

            float Y_val = yBlock[y * 8 + x] + 128.0f;

            // 双线性 Cb
            float cb00 = cbBlock[cy  * 8 + cx];
            float cb10 = cbBlock[cy  * 8 + cx1];
            float cb01 = cbBlock[cy1 * 8 + cx];
            float cb11 = cbBlock[cy1 * 8 + cx1];
            float Cb_val = (cb00 * (1.0f - cfx) + cb10 * cfx) * (1.0f - cfy)
                         + (cb01 * (1.0f - cfx) + cb11 * cfx) * cfy;

            // 双线性 Cr
            float cr00 = crBlock[cry  * 8 + crx];
            float cr10 = crBlock[cry  * 8 + crx1];
            float cr01 = crBlock[cry1 * 8 + crx];
            float cr11 = crBlock[cry1 * 8 + crx1];
            float Cr_val = (cr00 * (1.0f - crfx) + cr10 * crfx) * (1.0f - crfy)
                         + (cr01 * (1.0f - crfx) + cr11 * crfx) * crfy;

            float r = Y_val + 1.402f * Cr_val;
            float g = Y_val - 0.34414f * Cb_val - 0.71414f * Cr_val;
            float b = Y_val + 1.772f * Cb_val;

            int idx = (py * imgW + px) * 3;
            output[idx + 0] = (uchar)(r < 0.0f ? 0.0f : (r > 255.0f ? 255.0f : r + 0.5f));
            output[idx + 1] = (uchar)(g < 0.0f ? 0.0f : (g > 255.0f ? 255.0f : g + 0.5f));
            output[idx + 2] = (uchar)(b < 0.0f ? 0.0f : (b > 255.0f ? 255.0f : b + 0.5f));
        }
    }
}
