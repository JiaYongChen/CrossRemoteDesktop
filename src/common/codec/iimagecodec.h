#ifndef IIMAGECODEC_H
#define IIMAGECODEC_H

#include <QtGui/QImage>
#include <QtCore/QByteArray>

// 图像编解码接口：阶段A仅定义接口，不替换现有实现路径。
class IImageCodec {
public:
    virtual ~IImageCodec() = default;

    // 将图像编码为压缩字节
    virtual QByteArray encode(const QImage& image) = 0;

    // 从压缩字节解码为图像
    virtual QImage decode(const QByteArray& bytes) = 0;
};

#endif // IIMAGECODEC_H
