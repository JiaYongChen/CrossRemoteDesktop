#ifndef COMPRESSOR_FACTORY_H
#define COMPRESSOR_FACTORY_H

#include <memory>
#include <QtCore/QByteArray>

#include "icompressor.h"
#include "common/core/compression.h"

class CompressorFactory {
public:
    static std::unique_ptr<ICompressor> create(Compression::Algorithm algo);
};

#endif // COMPRESSOR_FACTORY_H
