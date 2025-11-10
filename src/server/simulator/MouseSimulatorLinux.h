#ifndef MOUSESIMULATORLINUX_H
#define MOUSESIMULATORLINUX_H

#include "MouseSimulator.h"
#include <QLoggingCategory>

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

Q_DECLARE_LOGGING_CATEGORY(lcMouseSimulatorLinux)

class MouseSimulatorLinux : public MouseSimulator {
public:
    MouseSimulatorLinux();
    ~MouseSimulatorLinux() override;

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
    Display* m_display;
    
    // 鼠标事件模拟
    bool simulateMouseEvent(int x, int y, unsigned int button, bool press);
    
    // Qt 按钮转 X11 按钮
    unsigned int qtButtonToX11Button(Qt::MouseButton button) const;
};

#endif // Q_OS_LINUX

#endif // MOUSESIMULATORLINUX_H
