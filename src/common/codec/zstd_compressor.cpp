#include "zstd_compressor.h"
#include <QtCore/QByteArray>
#ifdef HAVE_ZSTD
#include <zstd.h>
#endif

QByteArray ZstdCompressor::compress(const QByteArray& input, int level)
{
#ifdef HAVE_ZSTD
    if (input.isEmpty()) return {};
    if (level < 0) level = 3;
    size_t maxSize = ZSTD_compressBound(input.size());
    QByteArray out; out.resize(maxSize);
    size_t written = ZSTD_compress(out.data(), maxSize, input.data(), input.size(), level);
    if (ZSTD_isError(written)) return {};
    out.resize(written);
    return out;
#else
    Q_UNUSED(input); Q_UNUSED(level);
    return {};
#endif
}

QByteArray ZstdCompressor::decompress(const QByteArray& input)
{
#ifdef HAVE_ZSTD
    if (input.isEmpty()) return {};
    unsigned long long size = ZSTD_getFrameContentSize(input.data(), input.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN) return {};
    QByteArray out; out.resize(size);
    size_t written = ZSTD_decompress(out.data(), size, input.data(), input.size());
    if (ZSTD_isError(written) || written != size) return {};
    return out;
#else
    Q_UNUSED(input);
    return {};
#endif
}
