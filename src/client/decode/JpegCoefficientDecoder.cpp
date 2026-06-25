#include "JpegCoefficientDecoder.h"
#include "../../common/core/logging/LoggingCategories.h"

#include <QtCore/QByteArray>
#include <cstring>
#include <algorithm>

// ══════════════════════════════════════════════════════════════════════════════
// 匿名命名空间 — 内部实现
// ══════════════════════════════════════════════════════════════════════════════
namespace {

// kZigzag[natural_index] = zigzag_index  (JPEG 标准去 zigzag 映射)
constexpr int kZigzag[64] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

// kZigzagInv[zigzag_index] = natural_index  (逆映射，用于解码时直写自然顺序)
constexpr int kZigzagInv[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

constexpr uint16_t kSOI  = 0xFFD8;
constexpr uint16_t kEOI  = 0xFFD9;
constexpr uint16_t kDQT  = 0xFFDB;
constexpr uint16_t kSOF0 = 0xFFC0;
constexpr uint16_t kDHT  = 0xFFC4;
constexpr uint16_t kSOS  = 0xFFDA;

// ══════════════════════════════════════════════════════════════════════════════
// BitReader
// ══════════════════════════════════════════════════════════════════════════════

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size)
        : m_ptr(data), m_end(data + size) {
        fill();
    }

    int readBit() {
        if (m_bitsLeft == 0) fill();
        if (m_eof) return -1;  // 数据耗尽
        int b = (m_buffer >> (m_bitsLeft - 1)) & 1;
        --m_bitsLeft;
        return b;
    }

    /// 批量读取 n 位（1-16），复用 peekBits + consumeBits 模式
    /// 避免逐位循环，每条路径单次移位+掩码即可完成
    int readBits(int n) {
        while (m_bitsLeft < n && !m_eof) fill();
        if (m_eof) return -1;  // 数据耗尽
        int v = (m_buffer >> (m_bitsLeft - n)) & ((1 << n) - 1);
        m_bitsLeft -= n;
        return v;
    }

    /// 偷看 n 位（不消耗），确保缓冲区有足够位数
    int peekBits(int n) {
        while (m_bitsLeft < n && !m_eof) fill();
        if (m_eof) return -1;  // 数据耗尽
        return (m_buffer >> (m_bitsLeft - n)) & ((1 << n) - 1);
    }

    /// 消耗 n 位（与 peekBits 配对使用）
    void consumeBits(int n) {
        m_bitsLeft -= n;
    }

    void alignToByte() { m_bitsLeft &= ~7; }

    bool atMarker() {
        alignToByte();
        if (m_ptr >= m_end) return false;
        if (*m_ptr != 0xFF) return false;
        if (m_ptr + 1 >= m_end) return false;
        uint8_t next = *(m_ptr + 1);
        return next != 0x00 && next != 0xFF;
    }

    void skipCurrentMarker() {
        alignToByte();
        if (m_ptr + 1 < m_end) m_ptr += 2;
        m_buffer = 0; m_bitsLeft = 0;
        fill();
    }

private:
    const uint8_t* m_ptr;
    const uint8_t* m_end;
    uint32_t m_buffer = 0;
    int m_bitsLeft = 0;
    bool m_eof = false;  // 数据流耗尽标志

    void fill() {
        if (m_ptr >= m_end) {
            m_eof = true;  // 没有更多数据可读
            return;
        }
        while (m_bitsLeft <= 24 && m_ptr < m_end) {
            uint8_t b = *m_ptr++;
            if (b == 0xFF) {
                if (m_ptr < m_end && *m_ptr == 0x00) {
                    ++m_ptr;
                    m_buffer = (m_buffer << 8) | 0xFFu;
                    m_bitsLeft += 8;
                } else {
                    --m_ptr;  // 回退——这是标记
                    break;
                }
            } else {
                m_buffer = (m_buffer << 8) | b;
                m_bitsLeft += 8;
            }
        }
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// HuffTable
// ══════════════════════════════════════════════════════════════════════════════

// 快速查找表：11-bit 前缀 → (symbol << 8) | code_len
// kFastBits=11 覆盖绝大多数 JPEG Huffman 码（DC ≤9bit, AC 常见 ≤11bit）
// 表大小：2048 entries × 2 bytes = 4KB/表
constexpr int kFastBits = 11;
constexpr int kFastSize = 1 << kFastBits;

struct HuffTable {
    int     mincode[16];
    int     maxcode[16];
    int     valptr[16];
    uint8_t values[256];
    int     numValues = 0;
    uint16_t fast[kFastSize] = {};

    bool init(const uint8_t* bits, const uint8_t* vals, int count) {
        numValues = 0;
        int code = 0;
        int k = 0;
        for (int i = 0; i < 16; ++i) {
            if (bits[i] != 0) {
                valptr[i]  = k;
                mincode[i] = code;
                for (int j = 0; j < bits[i] && k < 256; ++j)
                    values[k++] = vals[valptr[i] + j];
                maxcode[i] = code + bits[i] - 1;
                numValues += bits[i];
            } else {
                valptr[i] = mincode[i] = maxcode[i] = -1;
            }
            code = (code + bits[i]) << 1;
        }
        buildFast();
        return (numValues <= count);
    }

    /// 解码一个 Huffman 符号（快速路径：11-bit 单次查表）
    int decode(BitReader& br) const {
        // 尝试 11-bit 快速查表
        int peek = br.peekBits(kFastBits);
        if (peek < 0) return -1;  // 数据耗尽
        uint16_t e = fast[peek];
        int len = e & 0xFF;
        if (len > 0) {
            br.consumeBits(len);
            return e >> 8;
        }
        // 慢速回退：逐位解码（极罕——仅 12-16 bit 码）
        int code = 0;
        // 前 11 位已经用作 peek，直接构建 code
        for (int i = 0; i < kFastBits; ++i) {
            code = (code << 1) | ((peek >> (kFastBits - 1 - i)) & 1);
        }
        br.consumeBits(kFastBits);
        for (int i = kFastBits; i < 16; ++i) {
            int b = br.readBit();
            if (b < 0) return -1;  // 数据耗尽
            code = (code << 1) | b;
            if (code <= maxcode[i])
                return values[valptr[i] + code - mincode[i]];
        }
        return -1;
    }

private:
    void buildFast() {
        // 对每个 11-bit 前缀，尝试匹配 Huffman 码
        for (int bits = 0; bits < kFastSize; ++bits) {
            int code = 0;
            for (int i = 0; i < kFastBits; ++i) {
                code = (code << 1) | ((bits >> (kFastBits - 1 - i)) & 1);
                if (i < 16 && maxcode[i] >= 0 && code <= maxcode[i]) {
                    fast[bits] = static_cast<uint16_t>(
                        (values[valptr[i] + code - mincode[i]] << 8) | (i + 1));
                    break;
                }
            }
            // 未匹配 → fast[bits] 保持 0（code_len=0 表示"非完整码"）
        }
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// 辅助函数
// ══════════════════════════════════════════════════════════════════════════════

inline uint16_t read16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline int extendSign(int v, int bits) {
    if (bits == 0) return 0;
    int half = 1 << (bits - 1);
    return (v >= half) ? v : v + 1 - (1 << bits);
}

struct Segment {
    const uint8_t* data = nullptr;
    int size = 0;
};

Segment readSegment(const uint8_t*& p, const uint8_t* end) {
    Segment seg;
    if (p + 3 > end) return seg;
    seg.size = static_cast<int>(read16(p)) - 2;
    p += 2;
    if (seg.size <= 0 || p + seg.size > end) {
        seg.size = 0; return seg;
    }
    seg.data = p;
    p += seg.size;
    return seg;
}

bool decodeBlock(BitReader& br, int& dcPred,
                 const HuffTable& dcTable, const HuffTable& acTable,
                 short* block)
{
    // AC 系数稀疏写入，需先清零
    std::memset(block, 0, 64 * sizeof(short));

    // DC 系数 → 自然顺序位置 0（zigzag 0）
    int ssss = dcTable.decode(br);
    if (ssss < 0 || ssss > 11) return false;
    if (ssss == 0) {
        block[0] = static_cast<short>(dcPred);
    } else {
        int delta = extendSign(br.readBits(ssss), ssss);
        dcPred += delta;
        block[0] = static_cast<short>(dcPred);
    }

    // AC 系数 → 通过逆 zigzag 表直写自然顺序
    int k = 1;
    while (k < 64) {
        int rs = acTable.decode(br);
        if (rs < 0) return false;
        if (rs == 0x00) break;

        int r = rs >> 4;
        int s = rs & 0x0F;

        if (s == 0 && r == 15) { k += 16; continue; }

        k += r;
        if (k >= 64) return false;

        block[kZigzagInv[k]] = static_cast<short>(extendSign(br.readBits(s), s));
        ++k;
    }

    return true;
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════════════
// 公开 API
// ══════════════════════════════════════════════════════════════════════════════

bool jpeg_extract_coefficients(const QByteArray& jpegData, JpegCoeffResult& out) {
    const auto* data = reinterpret_cast<const uint8_t*>(jpegData.constData());
    const size_t size = static_cast<size_t>(jpegData.size());
    const uint8_t* p = data;
    const uint8_t* end = data + size;
    if (size < 4) return false;

    // ── 局部状态 ──
    int imageW = 0, imageH = 0, nc = 0;
    int sampH[3] = {}, sampV[3] = {}, qtblId[3] = {};
    unsigned short qtbl[3][64] = {};
    int restartInterval = 0;
    HuffTable dcHTab[2], acHTab[2];  // 0=Y, 1=CbCr
    int dcTableId[3] = {}, acTableId[3] = {};
    const uint8_t* scanData = nullptr;
    const uint8_t* scanEnd  = nullptr;

    // ── 1) SOI ──
    if (read16(p) != kSOI) {
        qCWarning(lcClient) << "JpegCoeff: not JPEG — SOI=" << Qt::hex << read16(p);
        return false;
    }
    p += 2;

    // ── 2) 遍历标记段 ──
    for (;;) {
        if (p + 1 >= end) break;
        if (*p != 0xFF) break;

        uint8_t marker = p[0];
        uint8_t code   = p[1];

        if (code == 0x00) { p += 2; continue; }
        if (code >= 0xD0 && code <= 0xD7) break;  // RST — 不应在标记段

        p += 2;

        if (code == (kEOI & 0xFF)) break;

        if (code == (kSOS & 0xFF)) {
            // SOS: 读取段头，然后熵编码段开始
            int segLen = static_cast<int>(read16(p)) - 2;
            p += 2;
            if (segLen <= 0) { qCWarning(lcClient) << "JpegCoeff: SOS segLen<=0"; return false; }
            int ns = static_cast<int>(p[0]);
            if (ns < 1 || ns > 4) { qCWarning(lcClient) << "JpegCoeff: SOS ns out of range" << ns; return false; }
            for (int i = 0; i < ns; ++i) {
                int compId  = static_cast<int>(p[1 + i * 2]);
                int compIdx = compId - 1;
                int tdTa    = static_cast<int>(p[2 + i * 2]);
                if (compIdx >= 0 && compIdx < 3) {
                    dcTableId[compIdx] = tdTa >> 4;
                    acTableId[compIdx] = tdTa & 0x0F;
                }
            }
            p += 1 + ns * 2 + 3;  // +Ns + 组件选择器 + Ss/Se/AhAl
            scanData = p;
            scanEnd = end - 2;  // EOI 前
            break;
        }

        auto seg = readSegment(p, end);
        if (!seg.data) break;

        switch ((static_cast<int>(marker) << 8) | code) {
        case kDQT: {
            const uint8_t* d = seg.data;
            int rem = seg.size;
            while (rem >= 65) {
                int info = d[0], precision = info >> 4, tblId = info & 0x0F;
                ++d; --rem;
                if (tblId >= 3) { d += 64; rem -= 64; continue; }
                if (precision == 0) {
                    for (int i = 0; i < 64; ++i) qtbl[tblId][i] = d[i];
                    d += 64; rem -= 64;
                } else {
                    for (int i = 0; i < 64; ++i) { qtbl[tblId][i] = read16(d); d += 2; }
                    rem -= 128;
                }
                unsigned short tmp[64];
                for (int i = 0; i < 64; ++i) tmp[i] = qtbl[tblId][kZigzag[i]];
                std::memcpy(qtbl[tblId], tmp, sizeof(tmp));
            }
            break;
        }

        case kSOF0: {
            const uint8_t* d = seg.data;
            if (seg.size < 6) { qCWarning(lcClient) << "JpegCoeff: SOF0 seg.size<6"; return false; }
            int precision = d[0];
            imageH = static_cast<int>(read16(d + 1));
            imageW = static_cast<int>(read16(d + 3));
            nc = d[5];
            if (precision != 8 || imageW == 0 || imageH == 0) {
                qCWarning(lcClient) << "JpegCoeff: SOF0 bad params prec=" << precision
                                  << "w=" << imageW << "h=" << imageH;
                return false;
            }
            if (nc < 1 || nc > 3) { qCWarning(lcClient) << "JpegCoeff: SOF0 nc=" << nc; return false; }
            d += 6;
            for (int i = 0; i < nc; ++i) {
                int id = d[0] - 1, hv = d[1];
                sampH[id] = hv >> 4;
                sampV[id] = hv & 0x0F;
                qtblId[id] = d[2];
                d += 3;
            }
            break;
        }

        case kDHT: {
            const uint8_t* d = seg.data;
            int rem = seg.size;
            while (rem >= 17) {
                int info = d[0], tblCls = info >> 4, tblId = info & 0x0F;
                ++d; --rem;
                int numVals = 0;
                for (int i = 0; i < 16; ++i) numVals += d[i];
                if (rem < 16 + numVals) { rem = 0; break; }
                if (tblId >= 0 && tblId < 2) {
                    if (tblCls == 0) dcHTab[tblId].init(d, d + 16, numVals);
                    else             acHTab[tblId].init(d, d + 16, numVals);
                }
                d += 16 + numVals; rem -= 16 + numVals;
            }
            break;
        }

        case 0xFFDD: // DRI
            if (seg.size >= 2)
                restartInterval = static_cast<int>(read16(seg.data));
            break;

        default: break;
        }
    }

    // ── 3) 校验 ──
    if (imageW <= 0 || imageH <= 0 || nc <= 0) {
        qCWarning(lcClient) << "JpegCoeff: bad dims W=" << imageW << "H=" << imageH << "Nc=" << nc;
        return false;
    }
    if (!scanData || !scanEnd || scanData >= scanEnd) {
        qCWarning(lcClient) << "JpegCoeff: no scan data — scanData=" << (void*)scanData
                            << "scanEnd=" << (void*)scanEnd
                            << "fileSize=" << size;
        return false;
    }

    // ── 4) 计算块维度 ──
    int maxH = 1, maxV = 1;
    for (int i = 0; i < nc; ++i) {
        if (sampH[i] > maxH) maxH = sampH[i];
        if (sampV[i] > maxV) maxV = sampV[i];
    }
    int mcuW = maxH * 8, mcuH = maxV * 8;
    int mcuCols = (imageW + mcuW - 1) / mcuW;
    int mcuRows = (imageH + mcuH - 1) / mcuH;

    struct CompCtx { int h, v, bw, bh, dcPred = 0; std::vector<short>* buf; } comp[3];

    for (int i = 0; i < nc; ++i) {
        comp[i].h  = sampH[i];
        comp[i].v  = sampV[i];
        comp[i].bw = mcuCols * sampH[i];
        comp[i].bh = mcuRows * sampV[i];
    }

    out.coefY.resize(comp[0].bw * comp[0].bh * 64); comp[0].buf = &out.coefY;
    if (nc >= 3) {
        out.coefCb.resize(comp[1].bw * comp[1].bh * 64); comp[1].buf = &out.coefCb;
        out.coefCr.resize(comp[2].bw * comp[2].bh * 64); comp[2].buf = &out.coefCr;
    } else {
        out.coefCb.resize(64, 0); comp[1].buf = &out.coefCb;
        out.coefCr.resize(64, 0); comp[2].buf = &out.coefCr;
    }

    // ── 5) 复制量化表 ──
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 64; ++j)
            out.qtbl[i][j] = (i < nc) ? qtbl[qtblId[i]][j] : 0;
    if (nc == 1)
        for (int j = 0; j < 64; ++j) out.qtbl[1][j] = out.qtbl[2][j] = out.qtbl[0][j];

    out.width = imageW; out.height = imageH; out.numComponents = nc;

    // ── 6) 补充输出维度 ──
    out.yBlocksW = comp[0].bw; out.yBlocksH = comp[0].bh;
    if (nc >= 3) {
        out.cbBlocksW = comp[1].bw; out.cbBlocksH = comp[1].bh;
        out.crBlocksW = comp[2].bw; out.crBlocksH = comp[2].bh;
        out.cbHRatio = maxH / comp[1].h; out.cbVRatio = maxV / comp[1].v;
        out.crHRatio = maxH / comp[2].h; out.crVRatio = maxV / comp[2].v;
    } else {
        out.cbBlocksW = out.cbBlocksH = out.crBlocksW = out.crBlocksH = 1;
        out.cbHRatio = out.cbVRatio = out.crHRatio = out.crVRatio = 1;
    }

    // ── 7) Huffman 解码 ──
    BitReader br(scanData, static_cast<size_t>(scanEnd - scanData));
    int mcuIdx = 0;
    for (int my = 0; my < mcuRows; ++my) {
        for (int mx = 0; mx < mcuCols; ++mx) {
            if (restartInterval > 0 && mcuIdx > 0 && (mcuIdx % restartInterval) == 0) {
                for (int ci = 0; ci < 3; ++ci) comp[ci].dcPred = 0;
                br.alignToByte();
                if (br.atMarker()) br.skipCurrentMarker();
            }

            for (int ci = 0; ci < nc; ++ci) {
                CompCtx& c = comp[ci];
                int nblocks = c.h * c.v;
                for (int b = 0; b < nblocks; ++b) {
                    int bx = mx * c.h + (b % c.h);
                    int by = my * c.v + (b / c.h);

                    short block[64];
                    if (!decodeBlock(br, c.dcPred,
                                     dcHTab[dcTableId[ci]],
                                     acHTab[acTableId[ci]],
                                     block))
                    {
                        // 最后一个 MCU 的尾部组件允许 bitstream 自然耗尽——
                        // JPEG 扫描数据末尾可能有填充位/EOI 导致最后几块解码不到完整数据
                        if (my == mcuRows - 1 && mx == mcuCols - 1) {
                            std::memset(block, 0, sizeof(block));
                            block[0] = static_cast<short>(c.dcPred);
                        } else {
                            qCWarning(lcClient) << "JpegCoeff: Huffman decode failed at MCU ("
                                                << my << "," << mx << ") comp" << ci << "block" << b;
                            return false;
                        }
                    }

                    int off = (by * c.bw + bx) * 64;
                    std::memcpy(&(*c.buf)[off], block, sizeof(block));
                }
            }
            ++mcuIdx;
        }
    }

    return true;
}
