// VideoToolboxDecoder.mm — macOS GPU JPEG 解码器实现
#ifdef Q_OS_MACOS

#include "VideoToolboxDecoder.h"
#include "../IDecodeTarget.h"
#import <VideoToolbox/VideoToolbox.h>
#import <CoreVideo/CoreVideo.h>
#include <QDebug>

struct VideoToolboxDecoder::Impl {
    VTDecompressionSessionRef session = nullptr;
    CMVideoFormatDescriptionRef formatDesc = nullptr;
    OSStatus lastStatus = noErr;
};

VideoToolboxDecoder::VideoToolboxDecoder()
    : m_impl(std::make_unique<Impl>())
{
}

VideoToolboxDecoder::~VideoToolboxDecoder()
{
    if (m_impl->session) {
        VTDecompressionSessionInvalidate(m_impl->session);
        CFRelease(m_impl->session);
    }
    if (m_impl->formatDesc) {
        CFRelease(m_impl->formatDesc);
    }
}

bool VideoToolboxDecoder::decode(const QByteArray& jpegData,
                                   int* outWidth,
                                   int* outHeight,
                                   GLsync* outFence,
                                   QImage* outImage)
{
    Q_UNUSED(outImage);

    if (!m_target) {
        // 无解码目标时回退 CPU（调用方可传入 outImage 兜底）
        return false;
    }

    @autoreleasepool {
        // 从 JPEG 数据创建 CGImageSource
        NSData *data = [NSData dataWithBytes:jpegData.constData()
                                      length:static_cast<NSUInteger>(jpegData.size())];
        CGImageSourceRef imageSource = CGImageSourceCreateWithData(
            (__bridge CFDataRef)data, nullptr);
        if (!imageSource) return false;

        CGImageRef cgImage = CGImageSourceCreateImageAtIndex(
            imageSource, 0, nullptr);
        CFRelease(imageSource);
        if (!cgImage) return false;

        size_t width = CGImageGetWidth(cgImage);
        size_t height = CGImageGetHeight(cgImage);
        *outWidth = static_cast<int>(width);
        *outHeight = static_cast<int>(height);

        // 通过 IDecodeTarget 获取 PBO 映射指针
        unsigned char* dst = m_target->mapWriteBuffer(*outWidth, *outHeight);
        if (!dst) {
            CGImageRelease(cgImage);
            return false;
        }

        // 使用 CoreGraphics 绘制到目标缓冲区
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(
            dst, width, height, 8, *outWidth * 4,
            colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGColorSpaceRelease(colorSpace);

        if (context) {
            CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
            CGContextRelease(context);
        }

        CGImageRelease(cgImage);

        // 提交到 GL 纹理
        GLsync fence = m_target->commitWriteBuffer();
        if (outFence) *outFence = fence;
    }

    return true;
}

#endif // Q_OS_MACOS
