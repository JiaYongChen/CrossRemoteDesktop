#ifndef KEYBOARDSIMULATORLINUX_H
#define KEYBOARDSIMULATORLINUX_H

#include "KeyboardSimulator.h"
#include <QLoggingCategory>

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

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
    
    // Qt 修饰键转 X11 修饰键
    unsigned int qtModifiersToLinuxModifiers(Qt::KeyboardModifiers modifiers) const;
};

#endif // Q_OS_LINUX

#endif // KEYBOARDSIMULATORLINUX_H
