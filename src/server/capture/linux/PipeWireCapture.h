// PipeWireCapture.h — Linux PipeWire 屏幕捕获实现
#pragma once

#ifdef Q_OS_LINUX

#include "../IScreenCapture.h"
#include <QImage>
#include <QSize>
#include <QMutex>
#include <memory>
#include <spa/param/video/format-utils.h>

struct pw_context;
struct pw_core;
struct pw_stream;
struct pw_thread_loop;
struct spa_hook;

class PipeWireCapture : public IScreenCapture {
public:
    PipeWireCapture();
    ~PipeWireCapture() override;

    PipeWireCapture(const PipeWireCapture&) = delete;
    PipeWireCapture& operator=(const PipeWireCapture&) = delete;

    [[nodiscard]] bool initialize(int outputIndex = 0) override;
    void shutdown() override;
    [[nodiscard]] bool isInitialized() const override;
    CaptureResult captureFrame(int timeoutMs = 100) override;
    [[nodiscard]] CursorMessage sampleCursorPosition() const override;
    [[nodiscard]] QSize desktopSize() const override;
    [[nodiscard]] QString lastError() const override;
    [[nodiscard]] bool reinitialize() override;

    [[nodiscard]] static bool isAvailable();

private:
    void onProcessFrame(const unsigned char* data, int width, int height, int stride);

    pw_context*      m_pwContext = nullptr;
    pw_core*         m_pwCore = nullptr;
    pw_stream*       m_pwStream = nullptr;
    pw_thread_loop*  m_pwLoop = nullptr;
    spa_hook*        m_streamListener = nullptr;

    spa_video_info_raw m_format{};

    QImage  m_currentFrame;
    QSize   m_desktopSize;
    int32_t m_width = 0;
    int32_t m_height = 0;
    int32_t m_stride = 0;
    bool    m_initialized = false;
    bool    m_frameReady = false;
    QString m_lastError;

    /// 保护 PipeWire 回调线程与捕获线程间对 m_currentFrame / m_frameReady 的并发访问
    mutable QMutex m_mutex;
};

#endif // Q_OS_LINUX
