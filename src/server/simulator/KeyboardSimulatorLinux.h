#ifndef KEYBOARDSIMULATORLINUX_H
#define KEYBOARDSIMULATORLINUX_H

#include "KeyboardSimulator.h"
#include <QLoggingCategory>

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <unordered_map>

Q_DECLARE_LOGGING_CATEGORY(lcKeyboardSimulatorLinux)

class KeyboardSimulatorLinux : public KeyboardSimulator {
public:
    KeyboardSimulatorLinux();
    ~KeyboardSimulatorLinux() override;

    // 初始化和清理
    bool initialize() override;
    void cleanup() override;

    // 键盘操作
    bool simulateKeyPress(int qtKey, Qt::KeyboardModifiers modifiers) override;
    bool simulateKeyRelease(int qtKey, Qt::KeyboardModifiers modifiers) override;

private:
    Display* m_display;
    
    // 键盘事件模拟
    bool simulateKeyboardEvent(KeySym key, bool press, unsigned int modifiers);
    
    // Qt 按键转 X11 KeySym
    KeySym qtKeyToLinuxKey(int qtKey) const;
    
    // 小键盘按键处理（专用函数）
    KeySym handleNumpadKey(int baseKey, int originalKey) const;
    
    // 标准键盘按键处理（专用函数）
    KeySym handleStandardKey(int qtKey) const;
    
    // Qt 修饰键转 X11 修饰键
    unsigned int qtModifiersToLinuxModifiers(Qt::KeyboardModifiers modifiers) const;
    
    // 初始化按键映射表
    void initializeKeyMappings();
    
    // 按键映射表
    std::unordered_map<int, KeySym> m_standardKeyMap;    // 标准按键映射
    std::unordered_map<int, KeySym> m_numpadKeyMap;      // 小键盘按键映射
};

#endif // Q_OS_LINUX

#endif // KEYBOARDSIMULATORLINUX_H
