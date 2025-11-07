#include "KeyboardSimulatorMacOS.h"

#ifdef Q_OS_MACOS

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcKeyboardSimulatorMacOS, "simulator.keyboard.macos")

KeyboardSimulatorMacOS::KeyboardSimulatorMacOS() : KeyboardSimulator() {
    initializeKeyMappings();
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
    qDebug() << "KeyboardSimulatorMacOS: Initialized successfully";
    qDebug() << "Standard key mappings:" << m_standardKeyMap.size();
    qDebug() << "Numpad key mappings:" << m_numpadKeyMap.size();
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
        qCDebug(lcKeyboardSimulatorMacOS) << "simulateKeyPress: Not initialized or enabled";
        return false;
    }

    if (!isValidKey(qtKey)) {
        setLastError("Invalid key code");
        return false;
    }

    qCDebug(lcKeyboardSimulatorMacOS) << "simulateKeyPress: qtKey=" << Qt::hex << qtKey 
        << "(" << Qt::dec << qtKey << "), modifiers=" << modifiers;

    CGKeyCode macKey = qtKeyToMacOSKey(qtKey);
    CGEventFlags macModifiers = qtModifiersToMacOSModifiers(modifiers);
    
    qCDebug(lcKeyboardSimulatorMacOS) << "Mapped to macKey=" << Qt::hex << macKey 
        << "(" << Qt::dec << macKey << ")";
    
    return simulateKeyboardEvent(macKey, true, macModifiers);
}

bool KeyboardSimulatorMacOS::simulateKeyRelease(int qtKey, Qt::KeyboardModifiers modifiers) {
    if (!m_initialized || !m_enabled) {
        qCDebug(lcKeyboardSimulatorMacOS) << "simulateKeyRelease: Not initialized or enabled";
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

    // 检测主键是否是修饰键本身
    bool isMainKeyModifier = (key == 0x3B || key == 0x3E ||  // Control L/R
                              key == 0x38 || key == 0x3C ||  // Shift L/R
                              key == 0x3A || key == 0x3D ||  // Option L/R
                              key == 0x37 || key == 0x36);   // Command L/R

    qCDebug(lcKeyboardSimulatorMacOS) << "simulateKeyboardEvent: key=" << key 
        << "keyDown=" << keyDown << "modifiers=" << modifiers 
        << "isMainKeyModifier=" << isMainKeyModifier;

    // 对于修饰键本身，直接发送
    if (isMainKeyModifier) {
        CGEventRef event = CGEventCreateKeyboardEvent(nullptr, key, keyDown);
        if (event) {
            CGEventPost(kCGHIDEventTap, event);
            CFRelease(event);
            qCDebug(lcKeyboardSimulatorMacOS) << "Modifier key event sent: key=" << key;
            return true;
        }
        return false;
    }

    // 对于普通键，根据 modifiers 参数完整处理修饰键
    // 按下修饰键（仅在按键按下时）
    if (keyDown) {
        if (modifiers & kCGEventFlagMaskControl) {
            CGEventRef ctrlEvent = CGEventCreateKeyboardEvent(nullptr, 0x3B, true);  // Left Control
            if (ctrlEvent) {
                CGEventPost(kCGHIDEventTap, ctrlEvent);
                CFRelease(ctrlEvent);
                qCDebug(lcKeyboardSimulatorMacOS) << "Pressing Control";
            }
        }
        if (modifiers & kCGEventFlagMaskShift) {
            CGEventRef shiftEvent = CGEventCreateKeyboardEvent(nullptr, 0x38, true);  // Left Shift
            if (shiftEvent) {
                CGEventPost(kCGHIDEventTap, shiftEvent);
                CFRelease(shiftEvent);
                qCDebug(lcKeyboardSimulatorMacOS) << "Pressing Shift";
            }
        }
        if (modifiers & kCGEventFlagMaskAlternate) {
            CGEventRef altEvent = CGEventCreateKeyboardEvent(nullptr, 0x3A, true);  // Left Option
            if (altEvent) {
                CGEventPost(kCGHIDEventTap, altEvent);
                CFRelease(altEvent);
                qCDebug(lcKeyboardSimulatorMacOS) << "Pressing Option";
            }
        }
        if (modifiers & kCGEventFlagMaskCommand) {
            CGEventRef cmdEvent = CGEventCreateKeyboardEvent(nullptr, 0x37, true);  // Left Command
            if (cmdEvent) {
                CGEventPost(kCGHIDEventTap, cmdEvent);
                CFRelease(cmdEvent);
                qCDebug(lcKeyboardSimulatorMacOS) << "Pressing Command";
            }
        }
    }

    // 主键事件
    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, key, keyDown);
    if (!event) {
        qCWarning(lcKeyboardSimulatorMacOS) << "Failed to create CGEvent for keyboard key:" << key;
        return false;
    }
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);

    // 释放修饰键（仅在按键释放时）
    if (!keyDown) {
        if (modifiers & kCGEventFlagMaskCommand) {
            CGEventRef cmdEvent = CGEventCreateKeyboardEvent(nullptr, 0x37, false);  // Left Command
            if (cmdEvent) {
                CGEventPost(kCGHIDEventTap, cmdEvent);
                CFRelease(cmdEvent);
                qCDebug(lcKeyboardSimulatorMacOS) << "Releasing Command";
            }
        }
        if (modifiers & kCGEventFlagMaskAlternate) {
            CGEventRef altEvent = CGEventCreateKeyboardEvent(nullptr, 0x3A, false);  // Left Option
            if (altEvent) {
                CGEventPost(kCGHIDEventTap, altEvent);
                CFRelease(altEvent);
                qCDebug(lcKeyboardSimulatorMacOS) << "Releasing Option";
            }
        }
        if (modifiers & kCGEventFlagMaskShift) {
            CGEventRef shiftEvent = CGEventCreateKeyboardEvent(nullptr, 0x38, false);  // Left Shift
            if (shiftEvent) {
                CGEventPost(kCGHIDEventTap, shiftEvent);
                CFRelease(shiftEvent);
                qCDebug(lcKeyboardSimulatorMacOS) << "Releasing Shift";
            }
        }
        if (modifiers & kCGEventFlagMaskControl) {
            CGEventRef ctrlEvent = CGEventCreateKeyboardEvent(nullptr, 0x3B, false);  // Left Control
            if (ctrlEvent) {
                CGEventPost(kCGHIDEventTap, ctrlEvent);
                CFRelease(ctrlEvent);
                qCDebug(lcKeyboardSimulatorMacOS) << "Releasing Control";
            }
        }
    }

    qCDebug(lcKeyboardSimulatorMacOS) << "Keyboard event simulated successfully";
    return true;
}

CGKeyCode KeyboardSimulatorMacOS::qtKeyToMacOSKey(int qtKey) const {
    // 检测是否是小键盘按键 (Qt::KeypadModifier = 0x20000000)
    bool isKeypad = (qtKey & 0x20000000) != 0;
    int baseKey = qtKey & ~0x20000000;  // 移除 KeypadModifier 标志
    
    qCDebug(lcKeyboardSimulatorMacOS) << "qtKeyToMacOSKey: qtKey=" << Qt::hex << qtKey 
        << "(" << Qt::dec << qtKey << "), isKeypad=" << isKeypad << ", baseKey=" << Qt::hex << baseKey;
    
    // ===========================================
    // 小键盘按键处理（带 KeypadModifier 标志）
    // ===========================================
    if (isKeypad) {
        return handleNumpadKey(baseKey, qtKey);
    }
    
    // ===========================================
    // 标准键盘按键处理（不带 KeypadModifier 标志）
    // ===========================================
    return handleStandardKey(qtKey);
}

CGKeyCode KeyboardSimulatorMacOS::handleNumpadKey(int baseKey, int originalKey) const {
    qCDebug(lcKeyboardSimulatorMacOS) << "Processing numpad key: baseKey=" << Qt::hex << baseKey 
        << ", originalKey=" << originalKey;
    
    // 步骤 1: 在小键盘专用映射表中查找
    auto it = m_numpadKeyMap.find(baseKey);
    if (it != m_numpadKeyMap.end()) {
        qCDebug(lcKeyboardSimulatorMacOS) << "Found in numpad map: baseKey=" << Qt::hex << baseKey
            << "-> CGKeyCode=" << Qt::hex << it->second;
        return it->second;
    }
    
    // 步骤 2: 小键盘映射表中未找到，检查是否是导航键
    qCDebug(lcKeyboardSimulatorMacOS) << "Not found in numpad map, checking if it's a navigation key";
    
    auto stdIt = m_standardKeyMap.find(baseKey);
    if (stdIt != m_standardKeyMap.end()) {
        qCDebug(lcKeyboardSimulatorMacOS) << "Found navigation key in standard map: baseKey=" << Qt::hex << baseKey
            << "-> CGKeyCode=" << Qt::hex << stdIt->second;
        return stdIt->second;
    }
    
    // 步骤 3: 仍未找到映射，记录警告并返回默认值
    qCWarning(lcKeyboardSimulatorMacOS) << "Unmapped numpad key:" << Qt::hex << originalKey 
        << "(baseKey=" << baseKey << "), using fallback";
    return static_cast<CGKeyCode>(baseKey & 0xFF);
}

CGKeyCode KeyboardSimulatorMacOS::handleStandardKey(int qtKey) const {
    qCDebug(lcKeyboardSimulatorMacOS) << "Processing standard keyboard key: qtKey=" << Qt::hex << qtKey;
    
    // 在标准键盘映射表中查找
    auto it = m_standardKeyMap.find(qtKey);
    if (it != m_standardKeyMap.end()) {
        qCDebug(lcKeyboardSimulatorMacOS) << "Found in standard map: qtKey=" << Qt::hex << qtKey 
            << "-> CGKeyCode=" << Qt::hex << it->second << "(" << Qt::dec << it->second << ")";
        return it->second;
    }
    
    // 未找到映射，记录警告并返回默认值
    qCWarning(lcKeyboardSimulatorMacOS) << "Unmapped standard key:" << Qt::hex << qtKey 
        << "(" << Qt::dec << qtKey << "), using fallback CGKeyCode=" << (qtKey & 0xFF);
    return static_cast<CGKeyCode>(qtKey & 0xFF);
}

CGEventFlags KeyboardSimulatorMacOS::qtModifiersToMacOSModifiers(Qt::KeyboardModifiers modifiers) const {
    CGEventFlags result = 0;
    
    // 过滤掉 KeypadModifier，它不是真正的修饰键，只是用来标识小键盘的标志
    Qt::KeyboardModifiers filteredModifiers = modifiers & ~Qt::KeypadModifier;
    
    if (filteredModifiers & Qt::ControlModifier) result |= kCGEventFlagMaskControl;
    if (filteredModifiers & Qt::ShiftModifier) result |= kCGEventFlagMaskShift;
    if (filteredModifiers & Qt::AltModifier) result |= kCGEventFlagMaskAlternate;
    if (filteredModifiers & Qt::MetaModifier) result |= kCGEventFlagMaskCommand;
    
    qCDebug(lcKeyboardSimulatorMacOS) << "Modifiers conversion: Qt=" << Qt::hex << modifiers 
        << "filtered=" << filteredModifiers 
        << "-> macOS=" << result 
        << "(Ctrl=" << bool(result & kCGEventFlagMaskControl) 
        << ", Shift=" << bool(result & kCGEventFlagMaskShift) 
        << ", Alt=" << bool(result & kCGEventFlagMaskAlternate) 
        << ", Cmd=" << bool(result & kCGEventFlagMaskCommand) << ")";
    
    return result;
}

void KeyboardSimulatorMacOS::initializeKeyMappings() {
    // ===========================================
    // 标准按键映射 (Standard Key Mappings)
    // ===========================================
    
    // 字母键 A-Z (ANSI 键盘布局)
    m_standardKeyMap[Qt::Key_A] = 0x00;
    m_standardKeyMap[Qt::Key_S] = 0x01;
    m_standardKeyMap[Qt::Key_D] = 0x02;
    m_standardKeyMap[Qt::Key_F] = 0x03;
    m_standardKeyMap[Qt::Key_H] = 0x04;
    m_standardKeyMap[Qt::Key_G] = 0x05;
    m_standardKeyMap[Qt::Key_Z] = 0x06;
    m_standardKeyMap[Qt::Key_X] = 0x07;
    m_standardKeyMap[Qt::Key_C] = 0x08;
    m_standardKeyMap[Qt::Key_V] = 0x09;
    m_standardKeyMap[Qt::Key_B] = 0x0B;
    m_standardKeyMap[Qt::Key_Q] = 0x0C;
    m_standardKeyMap[Qt::Key_W] = 0x0D;
    m_standardKeyMap[Qt::Key_E] = 0x0E;
    m_standardKeyMap[Qt::Key_R] = 0x0F;
    m_standardKeyMap[Qt::Key_Y] = 0x10;
    m_standardKeyMap[Qt::Key_T] = 0x11;
    m_standardKeyMap[Qt::Key_O] = 0x1F;
    m_standardKeyMap[Qt::Key_U] = 0x20;
    m_standardKeyMap[Qt::Key_I] = 0x22;
    m_standardKeyMap[Qt::Key_P] = 0x23;
    m_standardKeyMap[Qt::Key_L] = 0x25;
    m_standardKeyMap[Qt::Key_J] = 0x26;
    m_standardKeyMap[Qt::Key_K] = 0x28;
    m_standardKeyMap[Qt::Key_N] = 0x2D;
    m_standardKeyMap[Qt::Key_M] = 0x2E;

    // 主键盘数字键 0-9
    m_standardKeyMap[Qt::Key_1] = 0x12;
    m_standardKeyMap[Qt::Key_2] = 0x13;
    m_standardKeyMap[Qt::Key_3] = 0x14;
    m_standardKeyMap[Qt::Key_4] = 0x15;
    m_standardKeyMap[Qt::Key_5] = 0x17;
    m_standardKeyMap[Qt::Key_6] = 0x16;
    m_standardKeyMap[Qt::Key_7] = 0x1A;
    m_standardKeyMap[Qt::Key_8] = 0x1C;
    m_standardKeyMap[Qt::Key_9] = 0x19;
    m_standardKeyMap[Qt::Key_0] = 0x1D;

    // 功能键 F1-F20
    m_standardKeyMap[Qt::Key_F1] = 0x7A;
    m_standardKeyMap[Qt::Key_F2] = 0x78;
    m_standardKeyMap[Qt::Key_F3] = 0x63;
    m_standardKeyMap[Qt::Key_F4] = 0x76;
    m_standardKeyMap[Qt::Key_F5] = 0x60;
    m_standardKeyMap[Qt::Key_F6] = 0x61;
    m_standardKeyMap[Qt::Key_F7] = 0x62;
    m_standardKeyMap[Qt::Key_F8] = 0x64;
    m_standardKeyMap[Qt::Key_F9] = 0x65;
    m_standardKeyMap[Qt::Key_F10] = 0x6D;
    m_standardKeyMap[Qt::Key_F11] = 0x67;
    m_standardKeyMap[Qt::Key_F12] = 0x6F;
    m_standardKeyMap[Qt::Key_F13] = 0x69;
    m_standardKeyMap[Qt::Key_F14] = 0x6B;
    m_standardKeyMap[Qt::Key_F15] = 0x71;
    m_standardKeyMap[Qt::Key_F16] = 0x6A;
    m_standardKeyMap[Qt::Key_F17] = 0x40;
    m_standardKeyMap[Qt::Key_F18] = 0x4F;
    m_standardKeyMap[Qt::Key_F19] = 0x50;
    m_standardKeyMap[Qt::Key_F20] = 0x5A;

    // 控制键
    m_standardKeyMap[Qt::Key_Return] = 0x24;
    m_standardKeyMap[Qt::Key_Tab] = 0x30;
    m_standardKeyMap[Qt::Key_Space] = 0x31;
    m_standardKeyMap[Qt::Key_Backspace] = 0x33;
    m_standardKeyMap[Qt::Key_Delete] = 0x75;      // Forward Delete
    m_standardKeyMap[Qt::Key_Escape] = 0x35;
    m_standardKeyMap[Qt::Key_Insert] = 0x72;      // Help/Insert
    m_standardKeyMap[Qt::Key_Home] = 0x73;
    m_standardKeyMap[Qt::Key_End] = 0x77;
    m_standardKeyMap[Qt::Key_PageUp] = 0x74;
    m_standardKeyMap[Qt::Key_PageDown] = 0x79;

    // 方向键
    m_standardKeyMap[Qt::Key_Left] = 0x7B;
    m_standardKeyMap[Qt::Key_Right] = 0x7C;
    m_standardKeyMap[Qt::Key_Down] = 0x7D;
    m_standardKeyMap[Qt::Key_Up] = 0x7E;

    // 修饰键
    m_standardKeyMap[Qt::Key_Shift] = 0x38;       // Left Shift
    m_standardKeyMap[Qt::Key_Control] = 0x3B;     // Left Control
    m_standardKeyMap[Qt::Key_Alt] = 0x3A;         // Left Option
    m_standardKeyMap[Qt::Key_Meta] = 0x37;        // Left Command
    m_standardKeyMap[Qt::Key_AltGr] = 0x3D;       // Right Option

    // 锁定键
    m_standardKeyMap[Qt::Key_CapsLock] = 0x39;
    m_standardKeyMap[Qt::Key_Clear] = 0x47;       // NumLock/Clear

    // 符号键 (基础键)
    m_standardKeyMap[Qt::Key_Semicolon] = 0x29;   // ;
    m_standardKeyMap[Qt::Key_Equal] = 0x18;       // =
    m_standardKeyMap[Qt::Key_Comma] = 0x2B;       // ,
    m_standardKeyMap[Qt::Key_Minus] = 0x1B;       // -
    m_standardKeyMap[Qt::Key_Period] = 0x2F;      // .
    m_standardKeyMap[Qt::Key_Slash] = 0x2C;       // /
    m_standardKeyMap[Qt::Key_QuoteLeft] = 0x32;   // `
    m_standardKeyMap[Qt::Key_BracketLeft] = 0x21; // [
    m_standardKeyMap[Qt::Key_Backslash] = 0x2A;   // "\"
    m_standardKeyMap[Qt::Key_BracketRight] = 0x1E;// ]
    m_standardKeyMap[Qt::Key_Apostrophe] = 0x27;  // '

    // Shift 组合的符号键（物理上与基础键相同）
    m_standardKeyMap[Qt::Key_Plus] = 0x18;        // + (Shift + =)
    m_standardKeyMap[Qt::Key_Underscore] = 0x1B;  // _ (Shift + -)
    m_standardKeyMap[Qt::Key_Less] = 0x2B;        // < (Shift + ,)
    m_standardKeyMap[Qt::Key_Greater] = 0x2F;     // > (Shift + .)
    m_standardKeyMap[Qt::Key_Question] = 0x2C;    // ? (Shift + /)
    m_standardKeyMap[Qt::Key_Colon] = 0x29;       // : (Shift + ;)
    m_standardKeyMap[Qt::Key_AsciiTilde] = 0x32;  // ~ (Shift + `)
    m_standardKeyMap[Qt::Key_BraceLeft] = 0x21;   // { (Shift + [)
    m_standardKeyMap[Qt::Key_BraceRight] = 0x1E;  // } (Shift + ])
    m_standardKeyMap[Qt::Key_Bar] = 0x2A;         // | (Shift + \)
    m_standardKeyMap[Qt::Key_QuoteDbl] = 0x27;    // " (Shift + ')

    // Shift + 数字键的符号
    m_standardKeyMap[Qt::Key_Exclam] = 0x12;      // ! (Shift + 1)
    m_standardKeyMap[Qt::Key_At] = 0x13;          // @ (Shift + 2)
    m_standardKeyMap[Qt::Key_NumberSign] = 0x14;  // # (Shift + 3)
    m_standardKeyMap[Qt::Key_Dollar] = 0x15;      // $ (Shift + 4)
    m_standardKeyMap[Qt::Key_Percent] = 0x17;     // % (Shift + 5)
    m_standardKeyMap[Qt::Key_AsciiCircum] = 0x16; // ^ (Shift + 6)
    m_standardKeyMap[Qt::Key_Ampersand] = 0x1A;   // & (Shift + 7)
    m_standardKeyMap[Qt::Key_Asterisk] = 0x1C;    // * (Shift + 8, 主键盘)
    m_standardKeyMap[Qt::Key_ParenLeft] = 0x19;   // ( (Shift + 9)
    m_standardKeyMap[Qt::Key_ParenRight] = 0x1D;  // ) (Shift + 0)

    // 音量和媒体控制键
    m_standardKeyMap[Qt::Key_VolumeDown] = 0x49;
    m_standardKeyMap[Qt::Key_VolumeUp] = 0x48;
    m_standardKeyMap[Qt::Key_VolumeMute] = 0x4A;
    m_standardKeyMap[Qt::Key_Help] = 0x72;

    // ===========================================
    // 小键盘映射 (Numpad Key Mappings)
    // ===========================================
    
    // 小键盘数字键 (0-9)
    m_numpadKeyMap[Qt::Key_0] = 0x52;
    m_numpadKeyMap[Qt::Key_1] = 0x53;
    m_numpadKeyMap[Qt::Key_2] = 0x54;
    m_numpadKeyMap[Qt::Key_3] = 0x55;
    m_numpadKeyMap[Qt::Key_4] = 0x56;
    m_numpadKeyMap[Qt::Key_5] = 0x57;
    m_numpadKeyMap[Qt::Key_6] = 0x58;
    m_numpadKeyMap[Qt::Key_7] = 0x59;
    m_numpadKeyMap[Qt::Key_8] = 0x5B;
    m_numpadKeyMap[Qt::Key_9] = 0x5C;

    // 小键盘运算符（不受 NumLock 影响，始终有效）
    m_numpadKeyMap[Qt::Key_Asterisk] = 0x43;      // * (小键盘)
    m_numpadKeyMap[Qt::Key_Plus] = 0x45;          // + (小键盘)
    m_numpadKeyMap[Qt::Key_Minus] = 0x4E;         // - (小键盘)
    m_numpadKeyMap[Qt::Key_Period] = 0x41;        // . (小键盘)
    m_numpadKeyMap[Qt::Key_Slash] = 0x4B;         // / (小键盘)
    m_numpadKeyMap[Qt::Key_Enter] = 0x4C;         // Enter (小键盘)
    m_numpadKeyMap[Qt::Key_Equal] = 0x51;         // = (小键盘)
    m_numpadKeyMap[Qt::Key_Clear] = 0x47;         // Clear (小键盘)
    
    qCDebug(lcKeyboardSimulatorMacOS) << "Key mappings initialized:"
        << "Standard keys:" << m_standardKeyMap.size()
        << ", Numpad keys:" << m_numpadKeyMap.size();
    
    // 调试输出:检查关键按键的映射
    qCDebug(lcKeyboardSimulatorMacOS) << "Backspace mapping: Qt::Key_Backspace (" << Qt::Key_Backspace 
        << ") -> CGKeyCode" << Qt::hex << m_standardKeyMap[Qt::Key_Backspace];
    qCDebug(lcKeyboardSimulatorMacOS) << "Delete mapping: Qt::Key_Delete (" << Qt::Key_Delete 
        << ") -> CGKeyCode" << Qt::hex << m_standardKeyMap[Qt::Key_Delete];
    qCDebug(lcKeyboardSimulatorMacOS) << "Return mapping: Qt::Key_Return (" << Qt::Key_Return 
        << ") -> CGKeyCode" << Qt::hex << m_standardKeyMap[Qt::Key_Return];
}

#endif // Q_OS_MACOS
