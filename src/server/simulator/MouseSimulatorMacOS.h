#ifndef MOUSESIMULATORMAC_H
#define MOUSESIMULATORMAC_H

#include "MouseSimulator.h"
#include <QtCore/QLoggingCategory>

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>

Q_DECLARE_LOGGING_CATEGORY(lcMouseSimulatorMacOS)

class MouseSimulatorMacOS : public MouseSimulator {
public:
    MouseSimulatorMacOS();
    ~MouseSimulatorMacOS() override;

    // 初始化和清理
    bool initialize() override;
    void cleanup() override;

    // 鼠标操作
    bool simulateMouseMove(int x, int y) override;
    bool simulateMousePress(int x, int y, Qt::MouseButton button) override;
    bool simulateMouseRelease(int x, int y, Qt::MouseButton button) override;
    bool simulateMouseWheel(int x, int y, int deltaX, int deltaY) override;
    bool simulateMouseDoubleClick(int x, int y, Qt::MouseButton button) override;

    // 屏幕信息
    QSize getScreenSize() const override;
    QPoint getCursorPosition() const override;

    // 光标信息
    int getCurrentCursorType() const override;

private:
    // 辅助功能权限检查
    bool checkAccessibilityPermission();
    bool requestAccessibilityPermission();

    // 鼠标事件模拟
    bool simulateMouseEvent(int x, int y, CGEventType eventType, CGMouseButton button);

    // Qt 按钮转 macOS 按钮
    CGMouseButton qtButtonToMacOSButton(Qt::MouseButton button) const;
};

#endif // Q_OS_MACOS

#endif // MOUSESIMULATORMAC_H
