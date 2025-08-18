#include "compressor_factory.h"
#include "zlib_compressor.h"
#include "lz4_compressor.h"
#include "zstd_compressor.h"

std::unique_ptr<ICompressor> CompressorFactory::create(Compression::Algorithm algo)
{
    switch (algo) {
    case Compression::ZLIB:
    case Compression::GZIP:
    case Compression::DEFLATE:
        return std::make_unique<ZlibCompressor>();
    case Compression::LZ4:
#ifdef HAVE_LZ4
        return std::make_unique<LZ4Compressor>();
#else
        return nullptr;
#endif
    case Compression::ZSTD:
#ifdef HAVE_ZSTD
        return std::make_unique<ZstdCompressor>();
#else
        return nullptr;
#endif
    default:
        return nullptr;
    }
}
