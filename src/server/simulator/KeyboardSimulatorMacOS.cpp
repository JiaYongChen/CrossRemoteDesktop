#include "KeyboardSimulatorMacOS.h"

#ifdef Q_OS_MACOS

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcKeyboardSimulatorMacOS, "simulator.keyboard.macos")

KeyboardSimulatorMacOS::KeyboardSimulatorMacOS() : KeyboardSimulator() {
}

KeyboardSimulatorMacOS::~KeyboardSimulatorMacOS() {
    cleanup();
}

bool KeyboardSimulatorMacOS::initialize() {
    if (m_initialized) {
        return true;
    }

    // 检查辅助功能权限
    if (!checkAccessibilityPermission()) {
        setLastError("需要辅助功能权限才能模拟输入事件。请在系统偏好设置 > 安全性与隐私 > 隐私 > 辅助功能中授予权限。");
        qWarning() << "KeyboardSimulatorMacOS: 缺少辅助功能权限";

        // 尝试请求权限（会打开系统设置）
        requestAccessibilityPermission();
        return false;
    }

    m_initialized = true;
    qDebug() << "KeyboardSimulatorMacOS: macOS 辅助功能权限已授予";
    return true;
}

void KeyboardSimulatorMacOS::cleanup() {
    // macOS API 不需要特殊清理
    m_initialized = false;
}

bool KeyboardSimulatorMacOS::checkAccessibilityPermission() {
    // 检查当前进程是否被信任（有辅助功能权限）
    return AXIsProcessTrusted();
}

bool KeyboardSimulatorMacOS::requestAccessibilityPermission() {
    // 创建带提示的选项字典，会弹出系统对话框引导用户授权
    const void* keys[] = { kAXTrustedCheckOptionPrompt };
    const void* values[] = { kCFBooleanTrue };

    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    Boolean trusted = AXIsProcessTrustedWithOptions(options);

    if (options) {
        CFRelease(options);
    }

    return trusted;
}

bool KeyboardSimulatorMacOS::simulateKeyPress(int qtKey, Qt::KeyboardModifiers modifiers) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    if (!isValidKey(qtKey)) {
        setLastError("Invalid key code");
        return false;
    }

    CGKeyCode macKey = qtKeyToMacOSKey(qtKey);
    CGEventFlags macModifiers = qtModifiersToMacOSModifiers(modifiers);
    
    return simulateKeyboardEvent(macKey, true, macModifiers);
}

bool KeyboardSimulatorMacOS::simulateKeyRelease(int qtKey, Qt::KeyboardModifiers modifiers) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    if (!isValidKey(qtKey)) {
        setLastError("Invalid key code");
        return false;
    }

    CGKeyCode macKey = qtKeyToMacOSKey(qtKey);
    CGEventFlags macModifiers = qtModifiersToMacOSModifiers(modifiers);
    
    return simulateKeyboardEvent(macKey, false, macModifiers);
}

bool KeyboardSimulatorMacOS::simulateKeyboardEvent(CGKeyCode key, bool keyDown, CGEventFlags modifiers) {
    // 检查辅助功能权限
    if (!AXIsProcessTrusted()) {
        qCWarning(lcKeyboardSimulatorMacOS) << "Accessibility permission not granted, cannot simulate keyboard event";
        setLastError("需要辅助功能权限");
        return false;
    }

    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, key, keyDown);

    if (event) {
        // 设置修饰键标志（如果有）
        if (modifiers != 0) {
            CGEventSetFlags(event, modifiers);
        }
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        qCDebug(lcKeyboardSimulatorMacOS) << "Keyboard event simulated: key=" << key
            << "keyDown=" << keyDown << "modifiers=" << modifiers;
        return true;
    }

    qCWarning(lcKeyboardSimulatorMacOS) << "Failed to create CGEvent for keyboard key:" << key;
    return false;
}

CGKeyCode KeyboardSimulatorMacOS::qtKeyToMacOSKey(int qtKey) const {
    // macOS 虚拟键码映射表
    // 参考: https://developer.apple.com/documentation/coregraphics/cgkeycode
    switch (qtKey) {
        // 字母键 (ANSI 键盘布局)
        case Qt::Key_A: return 0x00;
        case Qt::Key_S: return 0x01;
        case Qt::Key_D: return 0x02;
        case Qt::Key_F: return 0x03;
        case Qt::Key_H: return 0x04;
        case Qt::Key_G: return 0x05;
        case Qt::Key_Z: return 0x06;
        case Qt::Key_X: return 0x07;
        case Qt::Key_C: return 0x08;
        case Qt::Key_V: return 0x09;
        case Qt::Key_B: return 0x0B;
        case Qt::Key_Q: return 0x0C;
        case Qt::Key_W: return 0x0D;
        case Qt::Key_E: return 0x0E;
        case Qt::Key_R: return 0x0F;
        case Qt::Key_Y: return 0x10;
        case Qt::Key_T: return 0x11;
        case Qt::Key_O: return 0x1F;
        case Qt::Key_U: return 0x20;
        case Qt::Key_I: return 0x22;
        case Qt::Key_P: return 0x23;
        case Qt::Key_L: return 0x25;
        case Qt::Key_J: return 0x26;
        case Qt::Key_K: return 0x28;
        case Qt::Key_N: return 0x2D;
        case Qt::Key_M: return 0x2E;

        // 数字键
        case Qt::Key_1: return 0x12;
        case Qt::Key_2: return 0x13;
        case Qt::Key_3: return 0x14;
        case Qt::Key_4: return 0x15;
        case Qt::Key_5: return 0x17;
        case Qt::Key_6: return 0x16;
        case Qt::Key_7: return 0x1A;
        case Qt::Key_8: return 0x1C;
        case Qt::Key_9: return 0x19;
        case Qt::Key_0: return 0x1D;

        // 符号键
        case Qt::Key_Equal: return 0x18;        // =
        case Qt::Key_Minus: return 0x1B;        // -
        case Qt::Key_BracketRight: return 0x1E; // ]
        case Qt::Key_BracketLeft: return 0x21;  // [
        case Qt::Key_Apostrophe: return 0x27;   // '
        case Qt::Key_Semicolon: return 0x29;    // ;
        case Qt::Key_Backslash: return 0x2A;    // "\"
        case Qt::Key_Comma: return 0x2B;        // ,
        case Qt::Key_Slash: return 0x2C;        // /
        case Qt::Key_Period: return 0x2F;       // .
        case Qt::Key_QuoteLeft: return 0x32;    // `
        case Qt::Key_Less: return 0x2B;         // < (same as comma with shift)
        case Qt::Key_Greater: return 0x2F;      // > (same as period with shift)

        // 功能键
        case Qt::Key_Return: return 0x24;
        case Qt::Key_Enter: return 0x4C;       // 小键盘 Enter
        case Qt::Key_Tab: return 0x30;
        case Qt::Key_Space: return 0x31;
        case Qt::Key_Backspace: return 0x33;
        case Qt::Key_Delete: return 0x75;      // Forward Delete
        case Qt::Key_Escape: return 0x35;
        case Qt::Key_CapsLock: return 0x39;
        case Qt::Key_Help: return 0x72;
        case Qt::Key_Insert: return 0x72;      // Help/Insert
        case Qt::Key_Clear: return 0x47;       // NumLock/Clear

        // 修饰键
        case Qt::Key_Control: return 0x3B;     // Left Control
        case Qt::Key_Shift: return 0x38;       // Left Shift
        case Qt::Key_Alt: return 0x3A;         // Left Option
        case Qt::Key_Meta: return 0x37;        // Left Command
        case Qt::Key_AltGr: return 0x3D;       // Right Option

        // 方向键
        case Qt::Key_Left: return 0x7B;
        case Qt::Key_Right: return 0x7C;
        case Qt::Key_Down: return 0x7D;
        case Qt::Key_Up: return 0x7E;

        // F功能键
        case Qt::Key_F1: return 0x7A;
        case Qt::Key_F2: return 0x78;
        case Qt::Key_F3: return 0x63;
        case Qt::Key_F4: return 0x76;
        case Qt::Key_F5: return 0x60;
        case Qt::Key_F6: return 0x61;
        case Qt::Key_F7: return 0x62;
        case Qt::Key_F8: return 0x64;
        case Qt::Key_F9: return 0x65;
        case Qt::Key_F10: return 0x6D;
        case Qt::Key_F11: return 0x67;
        case Qt::Key_F12: return 0x6F;
        case Qt::Key_F13: return 0x69;
        case Qt::Key_F14: return 0x6B;
        case Qt::Key_F15: return 0x71;
        case Qt::Key_F16: return 0x6A;
        case Qt::Key_F17: return 0x40;
        case Qt::Key_F18: return 0x4F;
        case Qt::Key_F19: return 0x50;
        case Qt::Key_F20: return 0x5A;

        // 特殊键
        case Qt::Key_Home: return 0x73;
        case Qt::Key_End: return 0x77;
        case Qt::Key_PageUp: return 0x74;
        case Qt::Key_PageDown: return 0x79;

        // 音量和媒体控制键
        case Qt::Key_VolumeDown: return 0x49;
        case Qt::Key_VolumeUp: return 0x48;
        case Qt::Key_VolumeMute: return 0x4A;

        default:
            // 对于未映射的键，记录警告并尝试直接转换
            qCWarning(lcKeyboardSimulatorMacOS) << "Unmapped Qt key:" << Qt::hex << qtKey << Qt::dec
                << "(" << qtKey << ") - trying direct conversion";
            return static_cast<CGKeyCode>(qtKey & 0xFF);
    }
}

CGEventFlags KeyboardSimulatorMacOS::qtModifiersToMacOSModifiers(Qt::KeyboardModifiers modifiers) const {
    CGEventFlags result = 0;
    if (modifiers & Qt::ControlModifier) result |= kCGEventFlagMaskControl;
    if (modifiers & Qt::ShiftModifier) result |= kCGEventFlagMaskShift;
    if (modifiers & Qt::AltModifier) result |= kCGEventFlagMaskAlternate;
    if (modifiers & Qt::MetaModifier) result |= kCGEventFlagMaskCommand;
    return result;
}

#endif // Q_OS_MACOS
