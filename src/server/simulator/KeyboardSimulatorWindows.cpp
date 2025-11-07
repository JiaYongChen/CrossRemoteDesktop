#include "KeyboardSimulatorWindows.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcKeyboardSimulatorWindows, "simulator.keyboard.windows")

KeyboardSimulatorWindows::KeyboardSimulatorWindows() : KeyboardSimulator() {
}

KeyboardSimulatorWindows::~KeyboardSimulatorWindows() {
    cleanup();
}

bool KeyboardSimulatorWindows::initialize() {
    if (m_initialized) {
        return true;
    }

    // Windows API 不需要特殊初始化
    m_initialized = true;
    qDebug() << "KeyboardSimulatorWindows: Initialized successfully";
    return true;
}

void KeyboardSimulatorWindows::cleanup() {
    // Windows API 不需要特殊清理
    m_initialized = false;
}

bool KeyboardSimulatorWindows::simulateKeyPress(int qtKey, Qt::KeyboardModifiers modifiers) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    if (!isValidKey(qtKey)) {
        setLastError("Invalid key code");
        return false;
    }

    WORD winKey = qtKeyToWindowsKey(qtKey);
    DWORD winModifiers = qtModifiersToWindowsModifiers(modifiers);
    
    return simulateKeyboardEvent(winKey, 0, winModifiers); // 0 表示按下
}

bool KeyboardSimulatorWindows::simulateKeyRelease(int qtKey, Qt::KeyboardModifiers modifiers) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    if (!isValidKey(qtKey)) {
        setLastError("Invalid key code");
        return false;
    }

    WORD winKey = qtKeyToWindowsKey(qtKey);
    DWORD winModifiers = qtModifiersToWindowsModifiers(modifiers);
    
    return simulateKeyboardEvent(winKey, KEYEVENTF_KEYUP, winModifiers);
}

bool KeyboardSimulatorWindows::simulateKeyboardEvent(WORD key, DWORD flags, DWORD modifiers) {
    std::vector<INPUT> inputs;

    // 按下修饰键
    if (modifiers & 0x0002) {  // Ctrl
        INPUT input = {0};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_CONTROL;
        input.ki.dwFlags = 0;
        inputs.push_back(input);
    }
    if (modifiers & 0x0004) {  // Shift
        INPUT input = {0};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_SHIFT;
        input.ki.dwFlags = 0;
        inputs.push_back(input);
    }
    if (modifiers & 0x0001) {  // Alt
        INPUT input = {0};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_MENU;
        input.ki.dwFlags = 0;
        inputs.push_back(input);
    }

    // 主键事件
    INPUT mainInput = {0};
    mainInput.type = INPUT_KEYBOARD;
    mainInput.ki.wVk = key;
    mainInput.ki.dwFlags = flags;
    inputs.push_back(mainInput);

    // 释放修饰键（仅在按键释放时）
    if (flags & KEYEVENTF_KEYUP) {
        if (modifiers & 0x0001) {  // Alt
            INPUT input = {0};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_MENU;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        }
        if (modifiers & 0x0004) {  // Shift
            INPUT input = {0};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_SHIFT;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        }
        if (modifiers & 0x0002) {  // Ctrl
            INPUT input = {0};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_CONTROL;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        }
    }

    UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent == inputs.size()) {
        qCDebug(lcKeyboardSimulatorWindows) << "Keyboard event simulated: key=" << key 
            << "flags=" << flags << "modifiers=" << modifiers;
        return true;
    }

    qCWarning(lcKeyboardSimulatorWindows) << "Failed to send keyboard input: key=" << key;
    return false;
}

WORD KeyboardSimulatorWindows::qtKeyToWindowsKey(int qtKey) const {
    // Qt Key 到 Windows Virtual-Key Code 的映射
    // 参考: https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
    switch (qtKey) {
        // 字母键 A-Z (0x41-0x5A)
        case Qt::Key_A: return 0x41;
        case Qt::Key_B: return 0x42;
        case Qt::Key_C: return 0x43;
        case Qt::Key_D: return 0x44;
        case Qt::Key_E: return 0x45;
        case Qt::Key_F: return 0x46;
        case Qt::Key_G: return 0x47;
        case Qt::Key_H: return 0x48;
        case Qt::Key_I: return 0x49;
        case Qt::Key_J: return 0x4A;
        case Qt::Key_K: return 0x4B;
        case Qt::Key_L: return 0x4C;
        case Qt::Key_M: return 0x4D;
        case Qt::Key_N: return 0x4E;
        case Qt::Key_O: return 0x4F;
        case Qt::Key_P: return 0x50;
        case Qt::Key_Q: return 0x51;
        case Qt::Key_R: return 0x52;
        case Qt::Key_S: return 0x53;
        case Qt::Key_T: return 0x54;
        case Qt::Key_U: return 0x55;
        case Qt::Key_V: return 0x56;
        case Qt::Key_W: return 0x57;
        case Qt::Key_X: return 0x58;
        case Qt::Key_Y: return 0x59;
        case Qt::Key_Z: return 0x5A;

        // 数字键 0-9 (0x30-0x39)
        case Qt::Key_0: return 0x30;
        case Qt::Key_1: return 0x31;
        case Qt::Key_2: return 0x32;
        case Qt::Key_3: return 0x33;
        case Qt::Key_4: return 0x34;
        case Qt::Key_5: return 0x35;
        case Qt::Key_6: return 0x36;
        case Qt::Key_7: return 0x37;
        case Qt::Key_8: return 0x38;
        case Qt::Key_9: return 0x39;

        // 功能键 F1-F24
        case Qt::Key_F1: return VK_F1;
        case Qt::Key_F2: return VK_F2;
        case Qt::Key_F3: return VK_F3;
        case Qt::Key_F4: return VK_F4;
        case Qt::Key_F5: return VK_F5;
        case Qt::Key_F6: return VK_F6;
        case Qt::Key_F7: return VK_F7;
        case Qt::Key_F8: return VK_F8;
        case Qt::Key_F9: return VK_F9;
        case Qt::Key_F10: return VK_F10;
        case Qt::Key_F11: return VK_F11;
        case Qt::Key_F12: return VK_F12;
        case Qt::Key_F13: return VK_F13;
        case Qt::Key_F14: return VK_F14;
        case Qt::Key_F15: return VK_F15;
        case Qt::Key_F16: return VK_F16;
        case Qt::Key_F17: return VK_F17;
        case Qt::Key_F18: return VK_F18;
        case Qt::Key_F19: return VK_F19;
        case Qt::Key_F20: return VK_F20;
        case Qt::Key_F21: return VK_F21;
        case Qt::Key_F22: return VK_F22;
        case Qt::Key_F23: return VK_F23;
        case Qt::Key_F24: return VK_F24;

        // 特殊键
        case Qt::Key_Return: return VK_RETURN;
        case Qt::Key_Enter: return VK_RETURN;
        case Qt::Key_Tab: return VK_TAB;
        case Qt::Key_Space: return VK_SPACE;
        case Qt::Key_Backspace: return VK_BACK;
        case Qt::Key_Delete: return VK_DELETE;
        case Qt::Key_Escape: return VK_ESCAPE;
        case Qt::Key_CapsLock: return VK_CAPITAL;
        case Qt::Key_Insert: return VK_INSERT;
        case Qt::Key_Home: return VK_HOME;
        case Qt::Key_End: return VK_END;
        case Qt::Key_PageUp: return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;

        // 方向键
        case Qt::Key_Left: return VK_LEFT;
        case Qt::Key_Right: return VK_RIGHT;
        case Qt::Key_Up: return VK_UP;
        case Qt::Key_Down: return VK_DOWN;

        // 修饰键
        case Qt::Key_Shift: return VK_SHIFT;
        case Qt::Key_Control: return VK_CONTROL;
        case Qt::Key_Alt: return VK_MENU;
        case Qt::Key_Meta: return VK_LWIN;  // Windows 键

        // 符号键
        case Qt::Key_Semicolon: return VK_OEM_1;      // ;:
        case Qt::Key_Plus: return VK_OEM_PLUS;        // =+
        case Qt::Key_Comma: return VK_OEM_COMMA;      // ,<
        case Qt::Key_Minus: return VK_OEM_MINUS;      // -_
        case Qt::Key_Period: return VK_OEM_PERIOD;    // .>
        case Qt::Key_Slash: return VK_OEM_2;          // /?
        case Qt::Key_Less: return VK_OEM_COMMA;       // < (same as comma with shift)
        case Qt::Key_Greater: return VK_OEM_PERIOD;   // > (same as period with shift)
        case Qt::Key_AsciiTilde: return VK_OEM_3;     // `~
        case Qt::Key_BracketLeft: return VK_OEM_4;    // [{
        case Qt::Key_Backslash: return VK_OEM_5;      // \|
        case Qt::Key_BracketRight: return VK_OEM_6;   // ]}
        case Qt::Key_Apostrophe: return VK_OEM_7;     // '"
        case Qt::Key_QuoteLeft: return VK_OEM_3;      // `~
        case Qt::Key_Equal: return VK_OEM_PLUS;       // = (same key as plus)

        // 小键盘
        case Qt::Key_0 + Qt::KeypadModifier: return VK_NUMPAD0;
        case Qt::Key_1 + Qt::KeypadModifier: return VK_NUMPAD1;
        case Qt::Key_2 + Qt::KeypadModifier: return VK_NUMPAD2;
        case Qt::Key_3 + Qt::KeypadModifier: return VK_NUMPAD3;
        case Qt::Key_4 + Qt::KeypadModifier: return VK_NUMPAD4;
        case Qt::Key_5 + Qt::KeypadModifier: return VK_NUMPAD5;
        case Qt::Key_6 + Qt::KeypadModifier: return VK_NUMPAD6;
        case Qt::Key_7 + Qt::KeypadModifier: return VK_NUMPAD7;
        case Qt::Key_8 + Qt::KeypadModifier: return VK_NUMPAD8;
        case Qt::Key_9 + Qt::KeypadModifier: return VK_NUMPAD9;
        case Qt::Key_Asterisk: return VK_MULTIPLY;
        case Qt::Key_Plus + Qt::KeypadModifier: return VK_ADD;
        case Qt::Key_Minus + Qt::KeypadModifier: return VK_SUBTRACT;
        case Qt::Key_Period + Qt::KeypadModifier: return VK_DECIMAL;
        case Qt::Key_Slash + Qt::KeypadModifier: return VK_DIVIDE;

        // 锁定键
        case Qt::Key_NumLock: return VK_NUMLOCK;
        case Qt::Key_ScrollLock: return VK_SCROLL;

        // 其他键
        case Qt::Key_Pause: return VK_PAUSE;
        case Qt::Key_Print: return VK_SNAPSHOT;

        default:
            qCDebug(lcKeyboardSimulatorWindows) << "Unmapped Qt key:" << qtKey << "using default mapping";
            return static_cast<WORD>(qtKey);
    }
}

DWORD KeyboardSimulatorWindows::qtModifiersToWindowsModifiers(Qt::KeyboardModifiers modifiers) const {
    DWORD result = 0;
    if (modifiers & Qt::ControlModifier) result |= 0x0002;
    if (modifiers & Qt::ShiftModifier) result |= 0x0004;
    if (modifiers & Qt::AltModifier) result |= 0x0001;
    return result;
}

#endif // Q_OS_WIN
