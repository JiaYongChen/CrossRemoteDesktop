#include "TitleBarTheme.h"

// _WIN32 由 MSVC 编译器定义，无需任何头文件即生效
//（不能用 Q_OS_WIN，因为它定义在 qsystemdetection.h 中，而该头文件尚未被 include）
#ifdef _WIN32
#include <dwmapi.h>
#include <QtWidgets/QWidget>
#include "common/logging/LoggingCategories.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

void TitleBarTheme::apply(QWidget *window, bool dark)
{
    // 仅顶层窗口（QMainWindow / QDialog）有原生标题栏
    if (!window || !window->isWindow()) {
        qCDebug(lcUIMainWindow) << "TitleBarTheme::apply - 跳过非顶层窗口";
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    BOOL useDark = dark ? TRUE : FALSE;

    // 1. 设置 DWM 沉浸式暗色模式
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                       &useDark, sizeof(useDark));
    qCInfo(lcUIMainWindow) << "TitleBarTheme::apply"
                           << "- window:" << window->objectName()
                           << "- HWND:" << reinterpret_cast<quintptr>(hwnd)
                           << "- dark:" << dark
                           << "- HRESULT:" << Qt::hex << static_cast<quint32>(hr);

    if (FAILED(hr)) {
        qCWarning(lcUIMainWindow) << "TitleBarTheme::apply - DwmSetWindowAttribute 失败, HRESULT:"
                                  << Qt::hex << static_cast<quint32>(hr);
        return;
    }

    // 2. 刷新 DWM 渲染管线（确保属性提交到 DWM 纹理）
    DwmFlush();

    // 3. 强制 Windows 重绘非客户区（标题栏边框），使新属性立即生效
    //    SWP_FRAMECHANGED 发送 WM_NCCALCSIZE → 触发完整非客户区重算
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                 | SWP_FRAMECHANGED | SWP_NOCOPYBITS);

    // 4. 发送 WM_NCACTIVATE 强制重绘标题栏按钮和文字颜色
    //    先模拟失活再激活，确保深色模式下的白色文字/按钮颜色生效
    SendMessageW(hwnd, WM_NCACTIVATE, WPARAM(FALSE), 0);
    SendMessageW(hwnd, WM_NCACTIVATE, WPARAM(TRUE), 0);
}

#else // 非 Windows 平台：空操作

void TitleBarTheme::apply(QWidget *window, bool dark)
{
    (void)window;
    (void)dark;
}

#endif
