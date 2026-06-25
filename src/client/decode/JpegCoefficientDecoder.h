#pragma once

#include <cstdint>
#include <vector>

class QByteArray;

/**
 * @brief JPEG DCT 系数解码结果
 *
 * 包含从 JPEG 比特流中提取的全部信息，可直接传递给 OpenCL 内核。
 * 系数为量化后的 DCT 值（自然顺序），量化表也为自然顺序。
 */
struct JpegCoeffResult {
    int width = 0;
    int height = 0;
    int numComponents = 0;  // 1=灰度，3=YCbCr

    // ── 块维度 ──
    int yBlocksW = 0, yBlocksH = 0;
    int cbBlocksW = 0, cbBlocksH = 0;
    int crBlocksW = 0, crBlocksH = 0;

    // ── 色度子采样比例（相对 Y）──
    int cbHRatio = 1, cbVRatio = 1;
    int crHRatio = 1, crVRatio = 1;

    // ── 量化表（3 分量 × 64，自然顺序）──
    unsigned short qtbl[3][64];

    // ── DCT 系数（量化值，自然顺序，块行主序）──
    std::vector<short> coefY;
    std::vector<short> coefCb;
    std::vector<short> coefCr;
};

/**
 * @brief 从 JPEG 内存数据中提取 DCT 系数
 *
 * 零外部依赖：直接解析 JPEG 比特流（SOI / DQT / SOF0 / DHT / SOS），
 * Huffman 解码熵编码段，输出量化 DCT 系数（自然顺序）。
 *
 * 替代 libjpeg 的 jpeg_read_header + jpeg_read_coefficients，
 * 避免虚拟数组（virt_barray）和逐块 memcpy 开销。
 *
 * 支持的格式：Baseline DCT（SOF0），隔行扫描（single interleaved scan），8-bit 精度。
 * 不支持：Progressive JPEG、多扫描、算术编码。
 *
 * @param jpegData  原始 JPEG 字节
 * @param[out] out  解码结果
 * @return 成功返回 true
 */
[[nodiscard]] bool jpeg_extract_coefficients(const QByteArray& jpegData,
                                               JpegCoeffResult& out);
