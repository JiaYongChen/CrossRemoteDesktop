// OpenCL JPEG 解码内核 — CPU Huffman + GPU 反量化/IDCT/YCbCr→RGB
// 每个 work-item 处理一个 8×8 Y 块

// ── IDCT 预计算矩阵 ──
__constant float idct_mat[8] = {
    0.3535533906f, 0.4903926402f, 0.4619397663f, 0.4157348062f,
    0.3535533906f, 0.2777851165f, 0.1913417162f, 0.0975451610f
};

// ── 1D IDCT（8 点）──
static void idct_1d(float p[8]) {
    float tmp[8];
    for (int x = 0; x < 8; x++) {
        float s = 0.0f;
        for (int k = 0; k < 8; k++)
            s += p[k] * idct_mat[k] * cos((2.0f * x + 1.0f) * k * M_PI_F / 16.0f);
        tmp[x] = s;
    }
    for (int x = 0; x < 8; x++) p[x] = tmp[x];
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
    int imgW,
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

    // ── 反量化（系数自然顺序 × 量化表自然顺序）──
    float yBlock[64], cbBlock[64], crBlock[64];
    for (int i = 0; i < 64; i++) {
        yBlock[i]  = (float)y_src[i]  * (float)qtbl[0 * 64 + i];
        cbBlock[i] = (float)cb_src[i] * (float)qtbl[1 * 64 + i];
        crBlock[i] = (float)cr_src[i] * (float)qtbl[2 * 64 + i];
    }

    // ── 2D IDCT ──
    idct_2d(yBlock);
    idct_2d(cbBlock);
    idct_2d(crBlock);

    // ── YCbCr→RGB（BT.601）+ 级别偏移 ──
    for (int y = 0; y < 8; y++) {
        int py = by * 8 + y;
        for (int x = 0; x < 8; x++) {
            int px = bx * 8 + x;
            if (px >= imgW) continue;

            int pidx = y * 8 + x;
            float Y_val  = yBlock[pidx]  + 128.0f;
            float Cb_val = cbBlock[pidx];
            float Cr_val = crBlock[pidx];

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
