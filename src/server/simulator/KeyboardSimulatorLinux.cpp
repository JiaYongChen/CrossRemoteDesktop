#include "KeyboardSimulatorLinux.h"

#ifdef Q_OS_LINUX

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcKeyboardSimulatorLinux, "simulator.keyboard.linux")

KeyboardSimulatorLinux::KeyboardSimulatorLinux() : KeyboardSimulator(), m_display(nullptr) {
}

KeyboardSimulatorLinux::~KeyboardSimulatorLinux() {
    cleanup();
}

bool KeyboardSimulatorLinux::initialize() {
    if (m_initialized) {
        return true;
    }

    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        setLastError("Failed to open X11 display");
        qWarning() << "KeyboardSimulatorLinux: Failed to open X11 display";
        return false;
    }

    m_initialized = true;
    qDebug() << "KeyboardSimulatorLinux: Initialized successfully";
    return true;
}

void KeyboardSimulatorLinux::cleanup() {
    if (m_display) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
    m_initialized = false;
}

bool KeyboardSimulatorLinux::simulateKeyPress(int qtKey, Qt::KeyboardModifiers modifiers) {
    if (!m_initialized || !m_enabled || !m_display) {
        return false;
    }

    if (!isValidKey(qtKey)) {
        setLastError("Invalid key code");
        return false;
    }

    KeySym linuxKey = qtKeyToLinuxKey(qtKey);
    unsigned int linuxModifiers = qtModifiersToLinuxModifiers(modifiers);
    
    return simulateKeyboardEvent(linuxKey, true, linuxModifiers);
}

bool KeyboardSimulatorLinux::simulateKeyRelease(int qtKey, Qt::KeyboardModifiers modifiers) {
    if (!m_initialized || !m_enabled || !m_display) {
        return false;
    }

    if (!isValidKey(qtKey)) {
        setLastError("Invalid key code");
        return false;
    }

    KeySym linuxKey = qtKeyToLinuxKey(qtKey);
    unsigned int linuxModifiers = qtModifiersToLinuxModifiers(modifiers);
    
    return simulateKeyboardEvent(linuxKey, false, linuxModifiers);
}

bool KeyboardSimulatorLinux::simulateKeyboardEvent(KeySym key, bool press, unsigned int modifiers) {
    if (!m_display) {
        return false;
    }

    KeyCode keycode = XKeysymToKeycode(m_display, key);
    if (keycode == 0) {
        qCWarning(lcKeyboardSimulatorLinux) << "Failed to convert KeySym to KeyCode:" << key;
        return false;
    }

    // 按下修饰键
    if (press) {
        if (modifiers & ControlMask) {
            KeyCode ctrlKey = XKeysymToKeycode(m_display, XK_Control_L);
            XTestFakeKeyEvent(m_display, ctrlKey, True, CurrentTime);
        }
        if (modifiers & ShiftMask) {
            KeyCode shiftKey = XKeysymToKeycode(m_display, XK_Shift_L);
            XTestFakeKeyEvent(m_display, shiftKey, True, CurrentTime);
        }
        if (modifiers & Mod1Mask) {  // Alt
            KeyCode altKey = XKeysymToKeycode(m_display, XK_Alt_L);
            XTestFakeKeyEvent(m_display, altKey, True, CurrentTime);
        }
    }

    // 主键事件
    bool result = XTestFakeKeyEvent(m_display, keycode, press ? True : False, CurrentTime) == True;

    // 释放修饰键
    if (!press) {
        if (modifiers & Mod1Mask) {  // Alt
            KeyCode altKey = XKeysymToKeycode(m_display, XK_Alt_L);
            XTestFakeKeyEvent(m_display, altKey, False, CurrentTime);
        }
        if (modifiers & ShiftMask) {
            KeyCode shiftKey = XKeysymToKeycode(m_display, XK_Shift_L);
            XTestFakeKeyEvent(m_display, shiftKey, False, CurrentTime);
        }
        if (modifiers & ControlMask) {
            KeyCode ctrlKey = XKeysymToKeycode(m_display, XK_Control_L);
            XTestFakeKeyEvent(m_display, ctrlKey, False, CurrentTime);
        }
    }

    XFlush(m_display);
    
    qCDebug(lcKeyboardSimulatorLinux) << "Keyboard event simulated: key=" << key 
        << "press=" << press << "modifiers=" << modifiers;
    return result;
}

KeySym KeyboardSimulatorLinux::qtKeyToLinuxKey(int qtKey) const {
    // Qt Key 到 X11 KeySym 的映射
    // 参考: /usr/include/X11/keysymdef.h
    switch (qtKey) {
        // 字母键 (小写)
        case Qt::Key_A: return XK_a;
        case Qt::Key_B: return XK_b;
        case Qt::Key_C: return XK_c;
        case Qt::Key_D: return XK_d;
        case Qt::Key_E: return XK_e;
        case Qt::Key_F: return XK_f;
        case Qt::Key_G: return XK_g;
        case Qt::Key_H: return XK_h;
        case Qt::Key_I: return XK_i;
        case Qt::Key_J: return XK_j;
        case Qt::Key_K: return XK_k;
        case Qt::Key_L: return XK_l;
        case Qt::Key_M: return XK_m;
        case Qt::Key_N: return XK_n;
        case Qt::Key_O: return XK_o;
        case Qt::Key_P: return XK_p;
        case Qt::Key_Q: return XK_q;
        case Qt::Key_R: return XK_r;
        case Qt::Key_S: return XK_s;
        case Qt::Key_T: return XK_t;
        case Qt::Key_U: return XK_u;
        case Qt::Key_V: return XK_v;
        case Qt::Key_W: return XK_w;
        case Qt::Key_X: return XK_x;
        case Qt::Key_Y: return XK_y;
        case Qt::Key_Z: return XK_z;

        // 数字键
        case Qt::Key_0: return XK_0;
        case Qt::Key_1: return XK_1;
        case Qt::Key_2: return XK_2;
        case Qt::Key_3: return XK_3;
        case Qt::Key_4: return XK_4;
        case Qt::Key_5: return XK_5;
        case Qt::Key_6: return XK_6;
        case Qt::Key_7: return XK_7;
        case Qt::Key_8: return XK_8;
        case Qt::Key_9: return XK_9;

        // 功能键
        case Qt::Key_F1: return XK_F1;
        case Qt::Key_F2: return XK_F2;
        case Qt::Key_F3: return XK_F3;
        case Qt::Key_F4: return XK_F4;
        case Qt::Key_F5: return XK_F5;
        case Qt::Key_F6: return XK_F6;
        case Qt::Key_F7: return XK_F7;
        case Qt::Key_F8: return XK_F8;
        case Qt::Key_F9: return XK_F9;
        case Qt::Key_F10: return XK_F10;
        case Qt::Key_F11: return XK_F11;
        case Qt::Key_F12: return XK_F12;
        case Qt::Key_F13: return XK_F13;
        case Qt::Key_F14: return XK_F14;
        case Qt::Key_F15: return XK_F15;
        case Qt::Key_F16: return XK_F16;
        case Qt::Key_F17: return XK_F17;
        case Qt::Key_F18: return XK_F18;
        case Qt::Key_F19: return XK_F19;
        case Qt::Key_F20: return XK_F20;
        case Qt::Key_F21: return XK_F21;
        case Qt::Key_F22: return XK_F22;
        case Qt::Key_F23: return XK_F23;
        case Qt::Key_F24: return XK_F24;

        // 特殊键
        case Qt::Key_Return: return XK_Return;
        case Qt::Key_Enter: return XK_KP_Enter;
        case Qt::Key_Tab: return XK_Tab;
        case Qt::Key_Space: return XK_space;
        case Qt::Key_Backspace: return XK_BackSpace;
        case Qt::Key_Delete: return XK_Delete;
        case Qt::Key_Escape: return XK_Escape;
        case Qt::Key_CapsLock: return XK_Caps_Lock;
        case Qt::Key_Insert: return XK_Insert;
        case Qt::Key_Home: return XK_Home;
        case Qt::Key_End: return XK_End;
        case Qt::Key_PageUp: return XK_Page_Up;
        case Qt::Key_PageDown: return XK_Page_Down;

        // 方向键
        case Qt::Key_Left: return XK_Left;
        case Qt::Key_Right: return XK_Right;
        case Qt::Key_Up: return XK_Up;
        case Qt::Key_Down: return XK_Down;

        // 修饰键
        case Qt::Key_Shift: return XK_Shift_L;
        case Qt::Key_Control: return XK_Control_L;
        case Qt::Key_Alt: return XK_Alt_L;
        case Qt::Key_Meta: return XK_Super_L;

        // 符号键
        case Qt::Key_Semicolon: return XK_semicolon;
        case Qt::Key_Plus: return XK_plus;
        case Qt::Key_Comma: return XK_comma;
        case Qt::Key_Minus: return XK_minus;
        case Qt::Key_Period: return XK_period;
        case Qt::Key_Slash: return XK_slash;
        case Qt::Key_Less: return XK_less;
        case Qt::Key_Greater: return XK_greater;
        case Qt::Key_AsciiTilde: return XK_asciitilde;
        case Qt::Key_BracketLeft: return XK_bracketleft;
        case Qt::Key_Backslash: return XK_backslash;
        case Qt::Key_BracketRight: return XK_bracketright;
        case Qt::Key_Apostrophe: return XK_apostrophe;
        case Qt::Key_QuoteLeft: return XK_grave;
        case Qt::Key_Equal: return XK_equal;

        // 小键盘
        case Qt::Key_0 + Qt::KeypadModifier: return XK_KP_0;
        case Qt::Key_1 + Qt::KeypadModifier: return XK_KP_1;
        case Qt::Key_2 + Qt::KeypadModifier: return XK_KP_2;
        case Qt::Key_3 + Qt::KeypadModifier: return XK_KP_3;
        case Qt::Key_4 + Qt::KeypadModifier: return XK_KP_4;
        case Qt::Key_5 + Qt::KeypadModifier: return XK_KP_5;
        case Qt::Key_6 + Qt::KeypadModifier: return XK_KP_6;
        case Qt::Key_7 + Qt::KeypadModifier: return XK_KP_7;
        case Qt::Key_8 + Qt::KeypadModifier: return XK_KP_8;
        case Qt::Key_9 + Qt::KeypadModifier: return XK_KP_9;
        case Qt::Key_Asterisk: return XK_KP_Multiply;
        case Qt::Key_Plus + Qt::KeypadModifier: return XK_KP_Add;
        case Qt::Key_Minus + Qt::KeypadModifier: return XK_KP_Subtract;
        case Qt::Key_Period + Qt::KeypadModifier: return XK_KP_Decimal;
        case Qt::Key_Slash + Qt::KeypadModifier: return XK_KP_Divide;

        // 锁定键
        case Qt::Key_NumLock: return XK_Num_Lock;
        case Qt::Key_ScrollLock: return XK_Scroll_Lock;

        // 其他键
        case Qt::Key_Pause: return XK_Pause;
        case Qt::Key_Print: return XK_Print;

        default:
            qCDebug(lcKeyboardSimulatorLinux) << "Unmapped Qt key:" << qtKey << "using default mapping";
            return static_cast<KeySym>(qtKey);
    }
}

unsigned int KeyboardSimulatorLinux::qtModifiersToLinuxModifiers(Qt::KeyboardModifiers modifiers) const {
    unsigned int result = 0;
    if (modifiers & Qt::ControlModifier) result |= ControlMask;
    if (modifiers & Qt::ShiftModifier) result |= ShiftMask;
    if (modifiers & Qt::AltModifier) result |= Mod1Mask;
    return result;
}

#endif // Q_OS_LINUX
