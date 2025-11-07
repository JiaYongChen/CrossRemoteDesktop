#ifndef KEYBOARDSIMULATORMAC_H
#define KEYBOARDSIMULATORMAC_H

#include "KeyboardSimulator.h"
#include <QtCore/QLoggingCategory>

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>

Q_DECLARE_LOGGING_CATEGORY(lcKeyboardSimulatorMacOS)

class KeyboardSimulatorMacOS : public KeyboardSimulator {
public:
    KeyboardSimulatorMacOS();
    ~KeyboardSimulatorMacOS() override;

    // 初始化和清理
    bool initialize() override;
    void cleanup() override;

    // 键盘操作
    bool simulateKeyPress(int qtKey, Qt::KeyboardModifiers modifiers) override;
    bool simulateKeyRelease(int qtKey, Qt::KeyboardModifiers modifiers) override;

private:
    // 辅助功能权限检查
    bool checkAccessibilityPermission();
    bool requestAccessibilityPermission();

    // 键盘事件模拟
    bool simulateKeyboardEvent(CGKeyCode key, bool keyDown, CGEventFlags modifiers);

    // Qt 按键转 macOS 按键码
    CGKeyCode qtKeyToMacOSKey(int qtKey) const;

    // Qt 修饰键转 macOS 修饰键
    CGEventFlags qtModifiersToMacOSModifiers(Qt::KeyboardModifiers modifiers) const;
};

#endif // Q_OS_MACOS

#endif // KEYBOARDSIMULATORMAC_H
