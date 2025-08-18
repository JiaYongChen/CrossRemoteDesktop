#include "lz4_compressor.h"
#include <QtCore/QByteArray>
#ifdef HAVE_LZ4
#include <lz4.h>
#include <lz4hc.h>
#endif
#include <cstring>

QByteArray LZ4Compressor::compress(const QByteArray& input, int level)
{
#ifdef HAVE_LZ4
    if (input.isEmpty()) return {};
    int maxSize = LZ4_compressBound(input.size());
    QByteArray out;
    out.resize(sizeof(quint32) + maxSize);
    // header: original size
    quint32 orig = static_cast<quint32>(input.size());
    std::memcpy(out.data(), &orig, sizeof(quint32));
    int hc = (level > 1);
    int written = hc
        ? LZ4_compress_HC(input.data(), out.data() + sizeof(quint32), input.size(), maxSize, std::min(level, LZ4HC_CLEVEL_MAX))
        : LZ4_compress_default(input.data(), out.data() + sizeof(quint32), input.size(), maxSize);
    if (written <= 0) return {};
    out.resize(sizeof(quint32) + written);
    return out;
#else
    Q_UNUSED(input); Q_UNUSED(level);
    return {};
#endif
}

QByteArray LZ4Compressor::decompress(const QByteArray& input)
{
#ifdef HAVE_LZ4
    if (input.size() < (int)sizeof(quint32)) return {};
    quint32 orig = 0; std::memcpy(&orig, input.data(), sizeof(quint32));
    if (orig == 0) return {};
    QByteArray out; out.resize(orig);
    int res = LZ4_decompress_safe(input.data() + sizeof(quint32), out.data(), input.size() - (int)sizeof(quint32), orig);
    if (res < 0 || res != (int)orig) return {};
    return out;
#else
    Q_UNUSED(input);
    return {};
#endif
}
