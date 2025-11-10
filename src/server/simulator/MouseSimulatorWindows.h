#ifndef MOUSESIMULATORWINDOWS_H
#define MOUSESIMULATORWINDOWS_H

#include "MouseSimulator.h"
#include <QLoggingCategory>

#ifdef Q_OS_WIN
#include <windows.h>

Q_DECLARE_LOGGING_CATEGORY(lcMouseSimulatorWindows)

class MouseSimulatorWindows : public MouseSimulator {
public:
    MouseSimulatorWindows();
    ~MouseSimulatorWindows() override;

    // 初始化和清理
    bool initialize() override;
    void cleanup() override;

    // 鼠标操作
    bool simulateMouseMove(int x, int y) override;
    bool simulateMousePress(int x, int y, Qt::MouseButton button) override;
    bool simulateMouseRelease(int x, int y, Qt::MouseButton button) override;
    bool simulateMouseWheel(int x, int y, int deltaX, int deltaY) override;
    
    // 屏幕信息
    QSize getScreenSize() const override;
    QPoint getCursorPosition() const override;

    // 光标信息
    int getCurrentCursorType() const override;

private:
    // 鼠标事件模拟
    bool simulateMouseEvent(int x, int y, DWORD flags, DWORD data = 0);
    
    // Qt 按钮转 Windows 标志
    DWORD qtButtonToWindowsFlags(Qt::MouseButton button, bool isPress) const;
};

#endif // Q_OS_WIN

#endif // MOUSESIMULATORWINDOWS_H
