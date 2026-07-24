#include "server/capture/IScreenCapture.h"

#include <QtCore/QtGlobal>

#ifdef Q_OS_WIN
#include "server/capture/windows/DxgiCapture.h"
#endif
#ifdef Q_OS_LINUX
#include "server/capture/linux/PipeWireCapture.h"
#endif
#ifdef Q_OS_MACOS
#include "server/capture/macos/AvFoundationCapture.h"
#endif

std::unique_ptr<IScreenCapture> createScreenCapture()
{
#ifdef Q_OS_WIN
    return std::make_unique<DxgiCapture>();
#elif defined(Q_OS_LINUX)
#ifdef HAS_PIPEWIRE
    if (PipeWireCapture::isAvailable())
        return std::make_unique<PipeWireCapture>();
#endif
    return nullptr;  // 回退 Qt
#elif defined(Q_OS_MACOS)
#ifdef HAS_SCREEN_CAPTURE_KIT
    if (AvFoundationCapture::isAvailable())
        return std::make_unique<AvFoundationCapture>();
#endif
    return nullptr;  // 回退 Qt
#else
    return nullptr;
#endif
}
