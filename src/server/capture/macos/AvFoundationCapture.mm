// AvFoundationCapture.mm — macOS ScreenCaptureKit 屏幕捕获实现
#ifdef Q_OS_MACOS

#include "AvFoundationCapture.h"
#import <AVFoundation/AVFoundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <QGuiApplication>
#include <QScreen>
#include <QMutex>
#include <QtDebug>

// PIMPL：将 ObjC 对象隔离在 .mm 文件中
struct AvFoundationCapture::Impl {
    SCStream * __weak stream = nil;
    SCStreamConfiguration *config = nil;
    SCContentFilter *filter = nil;
    dispatch_queue_t queue = nil;
    QImage latestFrame;
    bool frameReady = false;
    QMutex mutex;
};

AvFoundationCapture::AvFoundationCapture()
    : m_impl(std::make_unique<Impl>())
{
}

AvFoundationCapture::~AvFoundationCapture()
{
    shutdown();
}

bool AvFoundationCapture::isAvailable()
{
    if (@available(macOS 13.0, *)) {
        return true;
    }
    return false;
}

bool AvFoundationCapture::initialize(int outputIndex)
{
    Q_UNUSED(outputIndex);

    if (m_initialized) return true;

    if (@available(macOS 13.0, *)) {
        // 检查屏幕录制权限
        if (!CGPreflightScreenCaptureAccess()) {
            m_lastError = QStringLiteral("macOS: 需要屏幕录制权限");
            emit screenCapturePermissionRequired();
            return false;
        }

        m_impl->queue = dispatch_queue_create(
            "com.crossremotedesktop.capture", DISPATCH_QUEUE_SERIAL);

        // 获取可共享内容
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block NSArray<SCDisplay *> *displays = nil;

        [SCShareableContent getShareableContentWithCompletionHandler:^(
            SCShareableContent *content, NSError *error) {
            if (error) {
                m_lastError = QString::fromNSString(error.localizedDescription);
            } else {
                displays = content.displays;
            }
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

        if (!displays || displays.count == 0) {
            m_lastError = QStringLiteral("macOS: 未找到可捕获的显示器");
            return false;
        }

        // 使用主显示器
        SCDisplay *targetDisplay = displays.firstObject;
        m_impl->filter = [[SCContentFilter alloc]
            initWithDisplay:targetDisplay
            excludingApplications:@[]
            exceptingWindows:@[]];

        m_impl->config = [[SCStreamConfiguration alloc] init];
        m_impl->config.width = targetDisplay.width;
        m_impl->config.height = targetDisplay.height;
        m_impl->config.pixelFormat = 'BGRA';  // kCVPixelFormatType_32BGRA
        m_impl->config.queueDepth = 3;

        m_impl->stream = [[SCStream alloc]
            initWithFilter:m_impl->filter
            configuration:m_impl->config
            delegate:nil];

        // 添加流输出
        __weak auto weakImpl = m_impl.get();
        [m_impl->stream addStreamOutput:^(
            CMSampleBufferRef sampleBuffer,
            NSError *error) {
            auto impl = weakImpl;
            if (error || !impl) return;

            CVPixelBufferRef pixelBuffer =
                CMSampleBufferGetImageBuffer(sampleBuffer);
            if (!pixelBuffer) return;

            CVPixelBufferLockBaseAddress(pixelBuffer,
                kCVPixelBufferLock_ReadOnly);

            size_t width = CVPixelBufferGetWidth(pixelBuffer);
            size_t height = CVPixelBufferGetHeight(pixelBuffer);
            size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
            void *baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);

            QMutexLocker locker(&impl->mutex);
            impl->latestFrame = QImage(
                static_cast<const uchar*>(baseAddress),
                static_cast<int>(width),
                static_cast<int>(height),
                static_cast<int>(bytesPerRow),
                QImage::Format_ARGB32).copy();

            CVPixelBufferUnlockBaseAddress(pixelBuffer,
                kCVPixelBufferLock_ReadOnly);

            impl->frameReady = true;
        } sampleHandlerQueue:m_impl->queue];

        // 启动流
        dispatch_semaphore_t startSem = dispatch_semaphore_create(0);
        [m_impl->stream startCaptureWithCompletionHandler:^(NSError *error) {
            if (error) {
                m_lastError = QString::fromNSString(error.localizedDescription);
            }
            dispatch_semaphore_signal(startSem);
        }];
        dispatch_semaphore_wait(startSem, DISPATCH_TIME_FOREVER);

        if (!m_lastError.isEmpty()) {
            return false;
        }

        // 等待首帧
        int waited = 0;
        while (!m_impl->frameReady && waited < 3000) {
            [NSThread sleepForTimeInterval:0.05];
            waited += 50;
        }

        m_desktopSize = QSize(static_cast<int>(targetDisplay.width),
                              static_cast<int>(targetDisplay.height));
        m_initialized = true;
        return true;
    }

    m_lastError = QStringLiteral("macOS: ScreenCaptureKit 需要 macOS 13.0+");
    return false;
}

void AvFoundationCapture::shutdown()
{
    if (m_impl->stream) {
        [m_impl->stream stopCaptureWithCompletionHandler:^(NSError *) {
            // 等待停止完成
        }];
        m_impl->stream = nil;
    }
    m_impl->filter = nil;
    m_impl->config = nil;
    if (m_impl->queue) {
        m_impl->queue = nil;  // ARC 管理释放
    }
    m_initialized = false;
}

bool AvFoundationCapture::isInitialized() const
{
    return m_initialized;
}

CaptureResult AvFoundationCapture::captureFrame(int timeoutMs)
{
    Q_UNUSED(timeoutMs);
    CaptureResult result;
    if (!m_initialized || !m_impl) return result;

    QMutexLocker locker(&m_impl->mutex);
    if (m_impl->frameReady) {
        result.frame = m_impl->latestFrame;
        m_impl->frameReady = false;
    }
    return result;
}

CursorMessage AvFoundationCapture::sampleCursorPosition() const
{
    return {};
}

QSize AvFoundationCapture::desktopSize() const
{
    return m_desktopSize;
}

QString AvFoundationCapture::lastError() const
{
    return m_lastError;
}

bool AvFoundationCapture::reinitialize()
{
    shutdown();
    return initialize(0);
}

#endif // Q_OS_MACOS
