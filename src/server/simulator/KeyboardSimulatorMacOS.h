#ifndef KEYBOARDSIMULATORMAC_H
#define KEYBOARDSIMULATORMAC_H

#include "KeyboardSimulator.h"
#include <QtCore/QLoggingCategory>

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include <unordered_map>

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
    
    // 小键盘按键处理（专用函数）
    CGKeyCode handleNumpadKey(int baseKey, int originalKey) const;
    
    // 标准键盘按键处理（专用函数）
    CGKeyCode handleStandardKey(int qtKey) const;

    // Qt 修饰键转 macOS 修饰键
    CGEventFlags qtModifiersToMacOSModifiers(Qt::KeyboardModifiers modifiers) const;
    
    // 初始化按键映射表
    void initializeKeyMappings();
    
    // 按键映射表
    std::unordered_map<int, CGKeyCode> m_standardKeyMap;    // 标准按键映射
    std::unordered_map<int, CGKeyCode> m_numpadKeyMap;      // 小键盘按键映射
};

#endif // Q_OS_MACOS

#endif // KEYBOARDSIMULATORMAC_H
