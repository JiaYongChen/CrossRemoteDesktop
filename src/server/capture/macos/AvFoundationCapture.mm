#include "AvFoundationCapture.h"

#if defined(Q_OS_MACOS) && defined(HAS_SCREEN_CAPTURE_KIT)

#include <cstdint>
#include <utility>

#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <dispatch/dispatch.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>
#include <QtGui/QImage>

namespace {
constexpr int ContentQueryTimeoutSeconds = 10;
constexpr int StartStopTimeoutSeconds = 5;
constexpr int FirstFrameTimeoutMs = 3000;

dispatch_time_t timeoutFromNow(int seconds)
{
    return dispatch_time(DISPATCH_TIME_NOW,
                         static_cast<int64_t>(seconds) * NSEC_PER_SEC);
}

bool requestScreenCaptureAccess()
{
    bool granted = false;
    const auto request = [&granted]() {
        granted = CGRequestScreenCaptureAccess();
    };

    QCoreApplication* app = QCoreApplication::instance();
    if (app && app->thread() != QThread::currentThread()) {
        if (!QMetaObject::invokeMethod(app, request, Qt::BlockingQueuedConnection)) {
            return false;
        }
    } else {
        request();
    }
    return granted;
}
} // namespace

@interface RDCaptureStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate> {
@public
    QMutex _mutex;
    QImage _latestFrame;
    QString _lastError;
    bool _frameReady;
}

- (BOOL)hasFrame;
- (void)clearFrame;

@end

@implementation RDCaptureStreamOutput

- (instancetype)init
{
    self = [super init];
    if (self) {
        _frameReady = false;
    }
    return self;
}

- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                  ofType:(SCStreamOutputType)type
{
    (void)stream;
    if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sampleBuffer)) {
        return;
    }

    CFArrayRef attachmentsRef =
        CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    NSArray<NSDictionary*>* attachments =
        (__bridge NSArray<NSDictionary*>*)attachmentsRef;
    NSDictionary* frameInfo = attachments.firstObject;
    NSNumber* frameStatus = frameInfo[SCStreamFrameInfoStatus];
    if (!frameStatus
        || frameStatus.integerValue != static_cast<NSInteger>(SCFrameStatusComplete)) {
        return;
    }

    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) {
        return;
    }

    if (CVPixelBufferLockBaseAddress(pixelBuffer,
                                     kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        return;
    }

    const size_t width = CVPixelBufferGetWidth(pixelBuffer);
    const size_t height = CVPixelBufferGetHeight(pixelBuffer);
    const size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    const void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);

    if (baseAddress && width > 0 && height > 0) {
        QImage view(static_cast<const uchar*>(baseAddress),
                    static_cast<int>(width),
                    static_cast<int>(height),
                    static_cast<qsizetype>(bytesPerRow),
                    QImage::Format_ARGB32);
        QImage copy = view.copy();

        QMutexLocker locker(&_mutex);
        _latestFrame = std::move(copy);
        _frameReady = !_latestFrame.isNull();
    }

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    (void)stream;
    QMutexLocker locker(&_mutex);
    _lastError = QString::fromNSString(error.localizedDescription);
}

- (BOOL)hasFrame
{
    QMutexLocker locker(&_mutex);
    return _frameReady ? YES : NO;
}

- (void)clearFrame
{
    QMutexLocker locker(&_mutex);
    _latestFrame = QImage();
    _lastError.clear();
    _frameReady = false;
}

@end

struct AvFoundationCapture::Impl {
    SCStream* __strong stream = nil;
    SCStreamConfiguration* __strong config = nil;
    SCContentFilter* __strong filter = nil;
    RDCaptureStreamOutput* __strong output = nil;
    dispatch_queue_t queue = nullptr;
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
    if (m_initialized) {
        return true;
    }

    m_lastError.clear();

    if (!isAvailable()) {
        m_lastError = QStringLiteral("macOS: ScreenCaptureKit requires macOS 13.0+");
        return false;
    }

    const bool mayPrompt =
        !qEnvironmentVariableIsSet("CROSSRD_DISABLE_SCREEN_CAPTURE_PERMISSION_PROMPT");
    if (!CGPreflightScreenCaptureAccess()
        && (!mayPrompt || !requestScreenCaptureAccess())) {
        m_lastError = QStringLiteral("macOS: screen recording permission is required");
        emit screenCapturePermissionRequired();
        return false;
    }

    if (@available(macOS 13.0, *)) {
        dispatch_semaphore_t contentSemaphore = dispatch_semaphore_create(0);
        __block SCShareableContent* contentResult = nil;
        __block NSError* contentError = nil;

        [SCShareableContent
            getShareableContentExcludingDesktopWindows:NO
            onScreenWindowsOnly:YES
            completionHandler:^(SCShareableContent* content, NSError* error) {
                contentResult = content;
                contentError = error;
                dispatch_semaphore_signal(contentSemaphore);
            }];

        if (dispatch_semaphore_wait(contentSemaphore,
                                    timeoutFromNow(ContentQueryTimeoutSeconds)) != 0) {
            m_lastError = QStringLiteral("macOS: timed out while querying shareable displays");
            return false;
        }
        if (contentError) {
            m_lastError = QString::fromNSString(contentError.localizedDescription);
            return false;
        }

        NSArray<SCDisplay*>* displays = contentResult.displays;
        if (!displays || displays.count == 0) {
            m_lastError = QStringLiteral("macOS: no capturable display found");
            return false;
        }
        if (outputIndex < 0 || static_cast<NSUInteger>(outputIndex) >= displays.count) {
            m_lastError = QStringLiteral("macOS: display index is out of range");
            return false;
        }

        SCDisplay* targetDisplay = displays[static_cast<NSUInteger>(outputIndex)];
        m_impl->filter = [[SCContentFilter alloc]
            initWithDisplay:targetDisplay
            excludingApplications:@[]
            exceptingWindows:@[]];

        m_impl->config = [[SCStreamConfiguration alloc] init];
        m_impl->config.width = targetDisplay.width;
        m_impl->config.height = targetDisplay.height;
        m_impl->config.pixelFormat = kCVPixelFormatType_32BGRA;
        m_impl->config.queueDepth = 3;
        m_impl->config.showsCursor = YES;

        m_impl->output = [[RDCaptureStreamOutput alloc] init];
        m_impl->queue = dispatch_queue_create(
            "com.crossremotedesktop.capture", DISPATCH_QUEUE_SERIAL);
        m_impl->stream = [[SCStream alloc]
            initWithFilter:m_impl->filter
            configuration:m_impl->config
            delegate:m_impl->output];

        NSError* addOutputError = nil;
        const BOOL outputAdded = [m_impl->stream
            addStreamOutput:m_impl->output
            type:SCStreamOutputTypeScreen
            sampleHandlerQueue:m_impl->queue
            error:&addOutputError];
        if (!outputAdded) {
            m_lastError = addOutputError
                ? QString::fromNSString(addOutputError.localizedDescription)
                : QStringLiteral("macOS: failed to add ScreenCaptureKit output");
            shutdown();
            return false;
        }

        dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);
        __block NSError* startError = nil;
        [m_impl->stream startCaptureWithCompletionHandler:^(NSError* error) {
            startError = error;
            dispatch_semaphore_signal(startSemaphore);
        }];
        if (dispatch_semaphore_wait(startSemaphore,
                                    timeoutFromNow(StartStopTimeoutSeconds)) != 0) {
            m_lastError = QStringLiteral("macOS: timed out while starting ScreenCaptureKit");
            shutdown();
            return false;
        }
        if (startError) {
            m_lastError = QString::fromNSString(startError.localizedDescription);
            shutdown();
            return false;
        }

        int waitedMs = 0;
        while (![m_impl->output hasFrame] && waitedMs < FirstFrameTimeoutMs) {
            QThread::msleep(10);
            waitedMs += 10;
        }
        if (![m_impl->output hasFrame]) {
            m_lastError = QStringLiteral(
                "macOS: timed out while waiting for the first captured frame");
            shutdown();
            return false;
        }

        m_desktopSize = QSize(static_cast<int>(targetDisplay.width),
                              static_cast<int>(targetDisplay.height));
        m_initialized = true;
        return true;
    }

    return false;
}

void AvFoundationCapture::shutdown()
{
    if (!m_impl) {
        m_initialized = false;
        return;
    }

    if (m_impl->stream) {
        dispatch_semaphore_t stopSemaphore = dispatch_semaphore_create(0);
        [m_impl->stream stopCaptureWithCompletionHandler:^(NSError* error) {
            (void)error;
            dispatch_semaphore_signal(stopSemaphore);
        }];
        (void)dispatch_semaphore_wait(stopSemaphore,
                                      timeoutFromNow(StartStopTimeoutSeconds));

        if (m_impl->output) {
            NSError* removeOutputError = nil;
            (void)[m_impl->stream removeStreamOutput:m_impl->output
                                                 type:SCStreamOutputTypeScreen
                                                error:&removeOutputError];
        }
    }

    [m_impl->output clearFrame];
    m_impl->stream = nil;
    m_impl->output = nil;
    m_impl->filter = nil;
    m_impl->config = nil;
    m_impl->queue = nullptr;
    m_desktopSize = QSize();
    m_initialized = false;
}

bool AvFoundationCapture::isInitialized() const
{
    return m_initialized;
}

CaptureResult AvFoundationCapture::captureFrame(int timeoutMs)
{
    CaptureResult result;
    if (!m_initialized || !m_impl || !m_impl->output) {
        return result;
    }

    int waitedMs = 0;
    while (![m_impl->output hasFrame] && waitedMs < timeoutMs) {
        QThread::msleep(1);
        ++waitedMs;
    }

    QMutexLocker locker(&m_impl->output->_mutex);
    if (m_impl->output->_frameReady) {
        result.frame = m_impl->output->_latestFrame;
        m_impl->output->_frameReady = false;
    }
    if (!m_impl->output->_lastError.isEmpty()) {
        m_lastError = m_impl->output->_lastError;
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

#endif // defined(Q_OS_MACOS) && defined(HAS_SCREEN_CAPTURE_KIT)
