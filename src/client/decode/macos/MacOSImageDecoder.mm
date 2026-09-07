#include "MacOSImageDecoder.h"

#ifdef Q_OS_MACOS

#include <cstring>
#include <limits>
#include <utility>

#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

#include <QtCore/QByteArray>
#include <QtGui/QImage>

#include "client/decode/IDecodeTarget.h"

bool MacOSImageDecoder::decode(const QByteArray& jpegData,
                               int* outWidth,
                               int* outHeight,
                               GLsync* outFence,
                               QImage* outImage)
{
    if (!outWidth || !outHeight || !outFence || jpegData.isEmpty()) {
        return false;
    }

    *outWidth = 0;
    *outHeight = 0;
    *outFence = nullptr;
    if (outImage) {
        *outImage = QImage();
    }

    @autoreleasepool {
        const auto* bytes = reinterpret_cast<const UInt8*>(jpegData.constData());
        CFDataRef data = CFDataCreate(kCFAllocatorDefault,
                                      bytes,
                                      static_cast<CFIndex>(jpegData.size()));
        if (!data) {
            return false;
        }

        CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
        CFRelease(data);
        if (!source) {
            return false;
        }

        CGImageRef cgImage = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
        CFRelease(source);
        if (!cgImage) {
            return false;
        }

        const size_t width = CGImageGetWidth(cgImage);
        const size_t height = CGImageGetHeight(cgImage);
        if (width == 0 || height == 0
            || width > static_cast<size_t>(std::numeric_limits<int>::max())
            || height > static_cast<size_t>(std::numeric_limits<int>::max())) {
            CGImageRelease(cgImage);
            return false;
        }

        QImage rgba(static_cast<int>(width), static_cast<int>(height),
                    QImage::Format_RGBA8888);
        if (rgba.isNull()) {
            CGImageRelease(cgImage);
            return false;
        }

        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        if (!colorSpace) {
            CGImageRelease(cgImage);
            return false;
        }
        CGContextRef context = CGBitmapContextCreate(
            rgba.bits(), width, height, 8,
            static_cast<size_t>(rgba.bytesPerLine()), colorSpace,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(colorSpace);
        if (!context) {
            CGImageRelease(cgImage);
            return false;
        }

        // Quartz 位图上下文使用左下角原点，而 QImage 的首行位于顶部。
        CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(height));
        CGContextScaleCTM(context, 1.0, -1.0);
        CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
        CGContextRelease(context);
        CGImageRelease(cgImage);

        QImage rgb = rgba.convertToFormat(QImage::Format_RGB888);
        if (rgb.isNull()) {
            return false;
        }

        *outWidth = rgb.width();
        *outHeight = rgb.height();
        if (m_target) {
            const qsizetype packedStride = static_cast<qsizetype>(rgb.width()) * 3;
            if (rgb.bytesPerLine() == packedStride) {
                *outFence = m_target->uploadPixels(
                    rgb.constBits(), rgb.width(), rgb.height());
            } else {
                QByteArray packedPixels(
                    packedStride * static_cast<qsizetype>(rgb.height()),
                    Qt::Uninitialized);
                for (int row = 0; row < rgb.height(); ++row) {
                    std::memcpy(packedPixels.data() + packedStride * row,
                                rgb.constScanLine(row),
                                static_cast<size_t>(packedStride));
                }
                *outFence = m_target->uploadPixels(
                    reinterpret_cast<const unsigned char*>(packedPixels.constData()),
                    rgb.width(), rgb.height());
            }
        }
        if (outImage && !*outFence) {
            *outImage = std::move(rgb);
        }
        return true;
    }
}

#endif // Q_OS_MACOS
