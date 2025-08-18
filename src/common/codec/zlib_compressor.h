#ifndef ZLIB_COMPRESSOR_H
#define ZLIB_COMPRESSOR_H

#include "icompressor.h"

class ZlibCompressor : public ICompressor {
public:
    QByteArray compress(const QByteArray& input, int level = -1) override;
    QByteArray decompress(const QByteArray& input) override;
};

#endif // ZLIB_COMPRESSOR_H
