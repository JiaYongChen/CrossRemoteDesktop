#include "IScreenCapture.h"
#include <QtGlobal>

#ifdef Q_OS_WIN
#include "windows/DxgiCapture.h"
#endif

std::unique_ptr<IScreenCapture> createScreenCapture()
{
#ifdef Q_OS_WIN
    return std::make_unique<DxgiCapture>();
#else
    return nullptr;
#endif
}
