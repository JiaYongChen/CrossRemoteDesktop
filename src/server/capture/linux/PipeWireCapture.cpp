// PipeWireCapture.cpp — Linux PipeWire 屏幕捕获实现
#include "server/capture/linux/PipeWireCapture.h"

#if defined(Q_OS_LINUX) && defined(HAS_PIPEWIRE)

bool PipeWireCapture::isAvailable()
{
    // 开发包、守护进程或 WAYLAND_DISPLAY 都不能替代 portal 授权流程。
    // 在完整 portal 会话接入前必须 fail closed，让工厂选择 Qt 回退。
    return false;
}

bool PipeWireCapture::initialize(int outputIndex)
{
    Q_UNUSED(outputIndex);
    m_initialized = false;
    m_lastError = QStringLiteral(
        "PipeWire: xdg-desktop-portal 捕获授权与流节点接入尚未实现");
    return false;
}

void PipeWireCapture::shutdown()
{
    m_initialized = false;
}

bool PipeWireCapture::isInitialized() const
{
    return m_initialized;
}

CaptureResult PipeWireCapture::captureFrame(int timeoutMs)
{
    Q_UNUSED(timeoutMs);
    return {};
}

CursorMessage PipeWireCapture::sampleCursorPosition() const
{
    return {};
}

QSize PipeWireCapture::desktopSize() const
{
    return {};
}

QString PipeWireCapture::lastError() const
{
    return m_lastError;
}

bool PipeWireCapture::reinitialize()
{
    shutdown();
    return initialize(0);
}

#endif // defined(Q_OS_LINUX) && defined(HAS_PIPEWIRE)
