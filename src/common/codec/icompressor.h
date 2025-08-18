#ifndef ICOMPRESSOR_H
#define ICOMPRESSOR_H

#include <QtCore/QByteArray>

// 通用字节压缩接口：阶段A仅定义接口，不改动现有 Compression 类。
class ICompressor {
public:
    virtual ~ICompressor() = default;

    virtual QByteArray compress(const QByteArray& input, int level = -1) = 0;
    virtual QByteArray decompress(const QByteArray& input) = 0;
};

#endif // ICOMPRESSOR_H
