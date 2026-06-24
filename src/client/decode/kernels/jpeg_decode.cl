// OpenCL JPEG block decoder — CPU Huffman + GPU IDCT/RGB
// 每个 work-item 处理一个 8×8 像素块

__constant float idct_mat[8] = {
    0.3535533906f, 0.4903926402f, 0.4619397663f, 0.4157348062f,
    0.3535533906f, 0.2777851165f, 0.1913417162f, 0.0975451610f
};

static void transpose8(float p[64]) {
    for (int i = 0; i < 8; i++)
        for (int j = i + 1; j < 8; j++) {
            float t = p[i * 8 + j];
            p[i * 8 + j] = p[j * 8 + i];
            p[j * 8 + i] = t;
        }
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

__kernel void jpeg_decode_block(
    __global const float* coefs,
    __global uchar*       output,
    int imgW, int blocksPerRow)
{
    int bx = get_global_id(0);
    int by = get_global_id(1);
    int bidx = by * blocksPerRow + bx;

    float b[64];
    __global const float* c = coefs + bidx * 64;
    for (int i = 0; i < 64; i++) b[i] = c[i];

    // 行方向 1D-IDCT
    for (int y = 0; y < 8; y++) {
        float tmp[8];
        for (int x = 0; x < 8; x++) {
            float s = 0.0f;
            for (int k = 0; k < 8; k++)
                s += b[y * 8 + k] * idct_mat[k] * cos((2.0f * x + 1.0f) * k * 3.1415926536f / 16.0f);
            tmp[x] = s;
        }
        for (int x = 0; x < 8; x++) b[y * 8 + x] = tmp[x];
    }

    transpose8(b);

    // 列方向 1D-IDCT
    for (int y = 0; y < 8; y++) {
        float tmp[8];
        for (int x = 0; x < 8; x++) {
            float s = 0.0f;
            for (int k = 0; k < 8; k++)
                s += b[y * 8 + k] * idct_mat[k] * cos((2.0f * x + 1.0f) * k * 3.1415926536f / 16.0f);
            tmp[x] = s;
        }
        for (int x = 0; x < 8; x++) b[y * 8 + x] = tmp[x];
    }

    transpose8(b);

    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int px = bx * 8 + x, py = by * 8 + y;
            int idx = (py * imgW + px) * 3;
            float v = b[y * 8 + x] + 128.0f;
            v = clampf(v, 0.0f, 255.0f);
            output[idx + 0] = (uchar)(v + 0.5f);
            output[idx + 1] = (uchar)(v + 0.5f);
            output[idx + 2] = (uchar)(v + 0.5f);
        }
}
