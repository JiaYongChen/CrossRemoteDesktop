#ifndef ZSTD_COMPRESSOR_H
#define ZSTD_COMPRESSOR_H

#include "icompressor.h"

class ZstdCompressor : public ICompressor {
public:
    QByteArray compress(const QByteArray& input, int level = -1) override;
    QByteArray decompress(const QByteArray& input) override;
};

#endif // ZSTD_COMPRESSOR_H
