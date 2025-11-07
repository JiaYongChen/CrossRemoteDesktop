#ifndef KEYBOARDSIMULATORWINDOWS_H
#define KEYBOARDSIMULATORWINDOWS_H

#include "KeyboardSimulator.h"
#include <QLoggingCategory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <vector>
#include <unordered_map>

Q_DECLARE_LOGGING_CATEGORY(lcKeyboardSimulatorWindows)

class KeyboardSimulatorWindows : public KeyboardSimulator {
public:
    KeyboardSimulatorWindows();
    ~KeyboardSimulatorWindows() override;

    // 初始化和清理
    bool initialize() override;
    void cleanup() override;

    // 键盘操作
    bool simulateKeyPress(int qtKey, Qt::KeyboardModifiers modifiers) override;
    bool simulateKeyRelease(int qtKey, Qt::KeyboardModifiers modifiers) override;

private:
    // 键盘事件模拟
    bool simulateKeyboardEvent(WORD key, DWORD flags, DWORD modifiers);
    
    // Qt 按键转 Windows 虚拟键码
    WORD qtKeyToWindowsKey(int qtKey) const;
    
    // 小键盘按键处理（专用函数）
    WORD handleNumpadKey(int baseKey, int originalKey) const;
    
    // 标准键盘按键处理（专用函数）
    WORD handleStandardKey(int qtKey) const;
    
    // Qt 修饰键转 Windows 修饰键
    DWORD qtModifiersToWindowsModifiers(Qt::KeyboardModifiers modifiers) const;
    
    // 初始化按键映射表
    void initializeKeyMappings();
    
    // 按键映射表
    std::unordered_map<int, WORD> m_standardKeyMap;    // 标准按键映射
    std::unordered_map<int, WORD> m_numpadKeyMap;      // 小键盘按键映射
};

#endif // Q_OS_WIN

#endif // KEYBOARDSIMULATORWINDOWS_H
