// PipeWireCapture.h — Linux PipeWire 屏幕捕获实现
#pragma once

#include <QtCore/QtGlobal>

#if defined(Q_OS_LINUX) && defined(HAS_PIPEWIRE)

#include <QtCore/QString>

#include "server/capture/IScreenCapture.h"

/**
 * PipeWire 屏幕捕获占位实现。
 *
 * Wayland 下必须通过 xdg-desktop-portal 获取用户授权、PipeWire remote fd
 * 和 stream node id。该流程接入前，此类始终报告不可用，防止仅凭
 * PipeWire 开发包或 WAYLAND_DISPLAY 就误选一个无法工作的后端。
 */
class PipeWireCapture : public IScreenCapture {
public:
    PipeWireCapture() = default;
    ~PipeWireCapture() override = default;

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
    bool    m_initialized = false;
    QString m_lastError;
};

#endif // defined(Q_OS_LINUX) && defined(HAS_PIPEWIRE)
