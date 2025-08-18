#include "zlib_compressor.h"
#include <zlib.h>
#include <QtCore/QByteArray>
#include <algorithm>

QByteArray ZlibCompressor::compress(const QByteArray& input, int level)
{
    if (input.isEmpty()) return {};
    int zlevel = (level >= Z_NO_COMPRESSION && level <= Z_BEST_COMPRESSION) ? level : Z_DEFAULT_COMPRESSION;
    uLongf maxSize = compressBound(input.size());
    QByteArray out;
    out.resize(maxSize);
    int res = compress2(reinterpret_cast<Bytef*>(out.data()), &maxSize,
                        reinterpret_cast<const Bytef*>(input.constData()), input.size(), zlevel);
    if (res != Z_OK) return {};
    out.resize(maxSize);
    return out;
}

QByteArray ZlibCompressor::decompress(const QByteArray& input)
{
    if (input.isEmpty()) return {};
    // Progressive growth when original size is unknown
    // Start larger to accommodate highly compressible inputs
    uLongf size = std::max<uLongf>(static_cast<uLongf>(input.size()) * 16u, 4096u);
    for (int i = 0; i < 12; ++i) {
        QByteArray out;
        out.resize(size);
        uLongf dstLen = size;
        int res = uncompress(reinterpret_cast<Bytef*>(out.data()), &dstLen,
                             reinterpret_cast<const Bytef*>(input.constData()), input.size());
        if (res == Z_OK) {
            out.resize(dstLen);
            return out;
        } else if (res == Z_BUF_ERROR) {
            size *= 2; // buffer too small, try again
            continue;
        } else {
            return {};
        }
    }
    return {};
}
