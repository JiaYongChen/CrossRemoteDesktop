#ifndef LZ4_COMPRESSOR_H
#define LZ4_COMPRESSOR_H

#include "icompressor.h"

class LZ4Compressor : public ICompressor {
public:
    QByteArray compress(const QByteArray& input, int level = -1) override;
    QByteArray decompress(const QByteArray& input) override;
};

#endif // LZ4_COMPRESSOR_H
