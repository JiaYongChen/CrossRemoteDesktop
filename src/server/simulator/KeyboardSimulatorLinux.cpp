#include "KeyboardSimulatorLinux.h"

#ifdef Q_OS_LINUX

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcKeyboardSimulatorLinux, "simulator.keyboard.linux")

KeyboardSimulatorLinux::KeyboardSimulatorLinux() : KeyboardSimulator(), m_display(nullptr) {
    initializeKeyMappings();
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
    qDebug() << "Standard key mappings:" << m_standardKeyMap.size();
    qDebug() << "Numpad key mappings:" << m_numpadKeyMap.size();
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
        qCDebug(lcKeyboardSimulatorLinux) << "simulateKeyPress: Not initialized or enabled";
        return false;
    }

    if (!isValidKey(qtKey)) {
        setLastError("Invalid key code");
        return false;
    }

    qCDebug(lcKeyboardSimulatorLinux) << "simulateKeyPress: qtKey=" << Qt::hex << qtKey 
        << "(" << Qt::dec << qtKey << "), modifiers=" << modifiers;

    KeySym linuxKey = qtKeyToLinuxKey(qtKey);
    unsigned int linuxModifiers = qtModifiersToLinuxModifiers(modifiers);
    
    qCDebug(lcKeyboardSimulatorLinux) << "Mapped to linuxKey=" << Qt::hex << linuxKey 
        << "(" << Qt::dec << linuxKey << ")";
    
    return simulateKeyboardEvent(linuxKey, true, linuxModifiers);
}

bool KeyboardSimulatorLinux::simulateKeyRelease(int qtKey, Qt::KeyboardModifiers modifiers) {
    if (!m_initialized || !m_enabled || !m_display) {
        qCDebug(lcKeyboardSimulatorLinux) << "simulateKeyRelease: Not initialized or enabled";
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

    // 检测主键是否是修饰键本身
    bool isMainKeyModifier = (key == XK_Control_L || key == XK_Control_R ||
                              key == XK_Shift_L || key == XK_Shift_R ||
                              key == XK_Alt_L || key == XK_Alt_R ||
                              key == XK_Meta_L || key == XK_Meta_R ||
                              key == XK_Super_L || key == XK_Super_R);

    qCDebug(lcKeyboardSimulatorLinux) << "simulateKeyboardEvent: key=" << key 
        << "press=" << press << "modifiers=" << modifiers 
        << "isMainKeyModifier=" << isMainKeyModifier;

    // 对于修饰键本身，直接发送
    if (isMainKeyModifier) {
        bool result = XTestFakeKeyEvent(m_display, keycode, press ? True : False, CurrentTime) == True;
        XFlush(m_display);
        qCDebug(lcKeyboardSimulatorLinux) << "Modifier key event sent: key=" << key;
        return result;
    }

    // 对于普通键，根据 modifiers 参数完整处理修饰键
    // 按下修饰键（仅在按键按下时）
    if (press) {
        if (modifiers & ControlMask) {
            KeyCode ctrlKey = XKeysymToKeycode(m_display, XK_Control_L);
            XTestFakeKeyEvent(m_display, ctrlKey, True, CurrentTime);
            qCDebug(lcKeyboardSimulatorLinux) << "Pressing Control";
        }
        if (modifiers & ShiftMask) {
            KeyCode shiftKey = XKeysymToKeycode(m_display, XK_Shift_L);
            XTestFakeKeyEvent(m_display, shiftKey, True, CurrentTime);
            qCDebug(lcKeyboardSimulatorLinux) << "Pressing Shift";
        }
        if (modifiers & Mod1Mask) {  // Alt
            KeyCode altKey = XKeysymToKeycode(m_display, XK_Alt_L);
            XTestFakeKeyEvent(m_display, altKey, True, CurrentTime);
            qCDebug(lcKeyboardSimulatorLinux) << "Pressing Alt";
        }
    }

    // 主键事件
    bool result = XTestFakeKeyEvent(m_display, keycode, press ? True : False, CurrentTime) == True;

    // 释放修饰键（仅在按键释放时）
    if (!press) {
        if (modifiers & Mod1Mask) {  // Alt
            KeyCode altKey = XKeysymToKeycode(m_display, XK_Alt_L);
            XTestFakeKeyEvent(m_display, altKey, False, CurrentTime);
            qCDebug(lcKeyboardSimulatorLinux) << "Releasing Alt";
        }
        if (modifiers & ShiftMask) {
            KeyCode shiftKey = XKeysymToKeycode(m_display, XK_Shift_L);
            XTestFakeKeyEvent(m_display, shiftKey, False, CurrentTime);
            qCDebug(lcKeyboardSimulatorLinux) << "Releasing Shift";
        }
        if (modifiers & ControlMask) {
            KeyCode ctrlKey = XKeysymToKeycode(m_display, XK_Control_L);
            XTestFakeKeyEvent(m_display, ctrlKey, False, CurrentTime);
            qCDebug(lcKeyboardSimulatorLinux) << "Releasing Control";
        }
    }

    XFlush(m_display);
    
    qCDebug(lcKeyboardSimulatorLinux) << "Keyboard event simulated successfully";
    return result;
}

KeySym KeyboardSimulatorLinux::qtKeyToLinuxKey(int qtKey) const {
    // 检测是否是小键盘按键 (Qt::KeypadModifier = 0x20000000)
    bool isKeypad = (qtKey & 0x20000000) != 0;
    int baseKey = qtKey & ~0x20000000;  // 移除 KeypadModifier 标志
    
    qCDebug(lcKeyboardSimulatorLinux) << "qtKeyToLinuxKey: qtKey=" << Qt::hex << qtKey 
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

KeySym KeyboardSimulatorLinux::handleNumpadKey(int baseKey, int originalKey) const {
    qCDebug(lcKeyboardSimulatorLinux) << "Processing numpad key: baseKey=" << Qt::hex << baseKey 
        << ", originalKey=" << originalKey;
    
    // 步骤 1: 在小键盘专用映射表中查找
    auto it = m_numpadKeyMap.find(baseKey);
    if (it != m_numpadKeyMap.end()) {
        qCDebug(lcKeyboardSimulatorLinux) << "Found in numpad map: baseKey=" << Qt::hex << baseKey
            << "-> KeySym=" << Qt::hex << it->second;
        return it->second;
    }
    
    // 步骤 2: 小键盘映射表中未找到，检查是否是导航键
    qCDebug(lcKeyboardSimulatorLinux) << "Not found in numpad map, checking if it's a navigation key";
    
    auto stdIt = m_standardKeyMap.find(baseKey);
    if (stdIt != m_standardKeyMap.end()) {
        qCDebug(lcKeyboardSimulatorLinux) << "Found navigation key in standard map: baseKey=" << Qt::hex << baseKey
            << "-> KeySym=" << Qt::hex << stdIt->second;
        return stdIt->second;
    }
    
    // 步骤 3: 仍未找到映射，记录警告并返回默认值
    qCWarning(lcKeyboardSimulatorLinux) << "Unmapped numpad key:" << Qt::hex << originalKey 
        << "(baseKey=" << baseKey << "), using fallback";
    return static_cast<KeySym>(baseKey);
}

KeySym KeyboardSimulatorLinux::handleStandardKey(int qtKey) const {
    qCDebug(lcKeyboardSimulatorLinux) << "Processing standard keyboard key: qtKey=" << Qt::hex << qtKey;
    
    // 在标准键盘映射表中查找
    auto it = m_standardKeyMap.find(qtKey);
    if (it != m_standardKeyMap.end()) {
        qCDebug(lcKeyboardSimulatorLinux) << "Found in standard map: qtKey=" << Qt::hex << qtKey 
            << "-> KeySym=" << Qt::hex << it->second << "(" << Qt::dec << it->second << ")";
        return it->second;
    }
    
    // 未找到映射，记录警告并返回默认值
    qCWarning(lcKeyboardSimulatorLinux) << "Unmapped standard key:" << Qt::hex << qtKey 
        << "(" << Qt::dec << qtKey << "), using fallback KeySym=" << qtKey;
    return static_cast<KeySym>(qtKey);
}

unsigned int KeyboardSimulatorLinux::qtModifiersToLinuxModifiers(Qt::KeyboardModifiers modifiers) const {
    unsigned int result = 0;
    
    // 过滤掉 KeypadModifier，它不是真正的修饰键，只是用来标识小键盘的标志
    Qt::KeyboardModifiers filteredModifiers = modifiers & ~Qt::KeypadModifier;
    
    if (filteredModifiers & Qt::ControlModifier) result |= ControlMask;
    if (filteredModifiers & Qt::ShiftModifier) result |= ShiftMask;
    if (filteredModifiers & Qt::AltModifier) result |= Mod1Mask;
    
    qCDebug(lcKeyboardSimulatorLinux) << "Modifiers conversion: Qt=" << Qt::hex << modifiers 
        << "filtered=" << filteredModifiers 
        << "-> Linux=" << result 
        << "(Ctrl=" << bool(result & ControlMask) 
        << ", Shift=" << bool(result & ShiftMask) 
        << ", Alt=" << bool(result & Mod1Mask) << ")";
    
    return result;
}

void KeyboardSimulatorLinux::initializeKeyMappings() {
    // ===========================================
    // 标准按键映射 (Standard Key Mappings)
    // ===========================================
    
    // 字母键 A-Z (小写)
    m_standardKeyMap[Qt::Key_A] = XK_a;
    m_standardKeyMap[Qt::Key_B] = XK_b;
    m_standardKeyMap[Qt::Key_C] = XK_c;
    m_standardKeyMap[Qt::Key_D] = XK_d;
    m_standardKeyMap[Qt::Key_E] = XK_e;
    m_standardKeyMap[Qt::Key_F] = XK_f;
    m_standardKeyMap[Qt::Key_G] = XK_g;
    m_standardKeyMap[Qt::Key_H] = XK_h;
    m_standardKeyMap[Qt::Key_I] = XK_i;
    m_standardKeyMap[Qt::Key_J] = XK_j;
    m_standardKeyMap[Qt::Key_K] = XK_k;
    m_standardKeyMap[Qt::Key_L] = XK_l;
    m_standardKeyMap[Qt::Key_M] = XK_m;
    m_standardKeyMap[Qt::Key_N] = XK_n;
    m_standardKeyMap[Qt::Key_O] = XK_o;
    m_standardKeyMap[Qt::Key_P] = XK_p;
    m_standardKeyMap[Qt::Key_Q] = XK_q;
    m_standardKeyMap[Qt::Key_R] = XK_r;
    m_standardKeyMap[Qt::Key_S] = XK_s;
    m_standardKeyMap[Qt::Key_T] = XK_t;
    m_standardKeyMap[Qt::Key_U] = XK_u;
    m_standardKeyMap[Qt::Key_V] = XK_v;
    m_standardKeyMap[Qt::Key_W] = XK_w;
    m_standardKeyMap[Qt::Key_X] = XK_x;
    m_standardKeyMap[Qt::Key_Y] = XK_y;
    m_standardKeyMap[Qt::Key_Z] = XK_z;

    // 主键盘数字键 0-9
    m_standardKeyMap[Qt::Key_0] = XK_0;
    m_standardKeyMap[Qt::Key_1] = XK_1;
    m_standardKeyMap[Qt::Key_2] = XK_2;
    m_standardKeyMap[Qt::Key_3] = XK_3;
    m_standardKeyMap[Qt::Key_4] = XK_4;
    m_standardKeyMap[Qt::Key_5] = XK_5;
    m_standardKeyMap[Qt::Key_6] = XK_6;
    m_standardKeyMap[Qt::Key_7] = XK_7;
    m_standardKeyMap[Qt::Key_8] = XK_8;
    m_standardKeyMap[Qt::Key_9] = XK_9;

    // 功能键 F1-F24
    m_standardKeyMap[Qt::Key_F1] = XK_F1;
    m_standardKeyMap[Qt::Key_F2] = XK_F2;
    m_standardKeyMap[Qt::Key_F3] = XK_F3;
    m_standardKeyMap[Qt::Key_F4] = XK_F4;
    m_standardKeyMap[Qt::Key_F5] = XK_F5;
    m_standardKeyMap[Qt::Key_F6] = XK_F6;
    m_standardKeyMap[Qt::Key_F7] = XK_F7;
    m_standardKeyMap[Qt::Key_F8] = XK_F8;
    m_standardKeyMap[Qt::Key_F9] = XK_F9;
    m_standardKeyMap[Qt::Key_F10] = XK_F10;
    m_standardKeyMap[Qt::Key_F11] = XK_F11;
    m_standardKeyMap[Qt::Key_F12] = XK_F12;
    m_standardKeyMap[Qt::Key_F13] = XK_F13;
    m_standardKeyMap[Qt::Key_F14] = XK_F14;
    m_standardKeyMap[Qt::Key_F15] = XK_F15;
    m_standardKeyMap[Qt::Key_F16] = XK_F16;
    m_standardKeyMap[Qt::Key_F17] = XK_F17;
    m_standardKeyMap[Qt::Key_F18] = XK_F18;
    m_standardKeyMap[Qt::Key_F19] = XK_F19;
    m_standardKeyMap[Qt::Key_F20] = XK_F20;
    m_standardKeyMap[Qt::Key_F21] = XK_F21;
    m_standardKeyMap[Qt::Key_F22] = XK_F22;
    m_standardKeyMap[Qt::Key_F23] = XK_F23;
    m_standardKeyMap[Qt::Key_F24] = XK_F24;

    // 控制键
    m_standardKeyMap[Qt::Key_Return] = XK_Return;
    m_standardKeyMap[Qt::Key_Tab] = XK_Tab;
    m_standardKeyMap[Qt::Key_Space] = XK_space;
    m_standardKeyMap[Qt::Key_Backspace] = XK_BackSpace;
    m_standardKeyMap[Qt::Key_Delete] = XK_Delete;
    m_standardKeyMap[Qt::Key_Escape] = XK_Escape;
    m_standardKeyMap[Qt::Key_Insert] = XK_Insert;
    m_standardKeyMap[Qt::Key_Home] = XK_Home;
    m_standardKeyMap[Qt::Key_End] = XK_End;
    m_standardKeyMap[Qt::Key_PageUp] = XK_Page_Up;
    m_standardKeyMap[Qt::Key_PageDown] = XK_Page_Down;

    // 方向键
    m_standardKeyMap[Qt::Key_Left] = XK_Left;
    m_standardKeyMap[Qt::Key_Right] = XK_Right;
    m_standardKeyMap[Qt::Key_Up] = XK_Up;
    m_standardKeyMap[Qt::Key_Down] = XK_Down;

    // 修饰键
    m_standardKeyMap[Qt::Key_Shift] = XK_Shift_L;
    m_standardKeyMap[Qt::Key_Control] = XK_Control_L;
    m_standardKeyMap[Qt::Key_Alt] = XK_Alt_L;
    m_standardKeyMap[Qt::Key_Meta] = XK_Super_L;
    m_standardKeyMap[Qt::Key_AltGr] = XK_ISO_Level3_Shift;

    // 锁定键
    m_standardKeyMap[Qt::Key_CapsLock] = XK_Caps_Lock;
    m_standardKeyMap[Qt::Key_NumLock] = XK_Num_Lock;
    m_standardKeyMap[Qt::Key_ScrollLock] = XK_Scroll_Lock;

    // 符号键（基础键）
    m_standardKeyMap[Qt::Key_Semicolon] = XK_semicolon;   // ;
    m_standardKeyMap[Qt::Key_Plus] = XK_plus;             // + (主键盘)
    m_standardKeyMap[Qt::Key_Comma] = XK_comma;           // ,
    m_standardKeyMap[Qt::Key_Minus] = XK_minus;           // - (主键盘)
    m_standardKeyMap[Qt::Key_Period] = XK_period;         // . (主键盘)
    m_standardKeyMap[Qt::Key_Slash] = XK_slash;           // / (主键盘)
    m_standardKeyMap[Qt::Key_QuoteLeft] = XK_grave;       // `
    m_standardKeyMap[Qt::Key_BracketLeft] = XK_bracketleft;  // [
    m_standardKeyMap[Qt::Key_Backslash] = XK_backslash;   // "\"
    m_standardKeyMap[Qt::Key_BracketRight] = XK_bracketright;// ]
    m_standardKeyMap[Qt::Key_Apostrophe] = XK_apostrophe; // '
    m_standardKeyMap[Qt::Key_Equal] = XK_equal;           // =

    // Shift 组合的符号键（物理上与基础键相同）
    m_standardKeyMap[Qt::Key_Less] = XK_less;             // < (Shift + ,)
    m_standardKeyMap[Qt::Key_Greater] = XK_greater;       // > (Shift + .)
    m_standardKeyMap[Qt::Key_Question] = XK_question;     // ? (Shift + /)
    m_standardKeyMap[Qt::Key_Colon] = XK_colon;           // : (Shift + ;)
    m_standardKeyMap[Qt::Key_AsciiTilde] = XK_asciitilde; // ~ (Shift + `)
    m_standardKeyMap[Qt::Key_BraceLeft] = XK_braceleft;   // { (Shift + [)
    m_standardKeyMap[Qt::Key_BraceRight] = XK_braceright; // } (Shift + ])
    m_standardKeyMap[Qt::Key_Bar] = XK_bar;               // | (Shift + \)
    m_standardKeyMap[Qt::Key_QuoteDbl] = XK_quotedbl;     // " (Shift + ')
    m_standardKeyMap[Qt::Key_Underscore] = XK_underscore; // _ (Shift + -)

    // Shift + 数字键的符号
    m_standardKeyMap[Qt::Key_Exclam] = XK_exclam;         // ! (Shift + 1)
    m_standardKeyMap[Qt::Key_At] = XK_at;                 // @ (Shift + 2)
    m_standardKeyMap[Qt::Key_NumberSign] = XK_numbersign; // # (Shift + 3)
    m_standardKeyMap[Qt::Key_Dollar] = XK_dollar;         // $ (Shift + 4)
    m_standardKeyMap[Qt::Key_Percent] = XK_percent;       // % (Shift + 5)
    m_standardKeyMap[Qt::Key_AsciiCircum] = XK_asciicircum;// ^ (Shift + 6)
    m_standardKeyMap[Qt::Key_Ampersand] = XK_ampersand;   // & (Shift + 7)
    m_standardKeyMap[Qt::Key_Asterisk] = XK_asterisk;     // * (Shift + 8, 主键盘)
    m_standardKeyMap[Qt::Key_ParenLeft] = XK_parenleft;   // ( (Shift + 9)
    m_standardKeyMap[Qt::Key_ParenRight] = XK_parenright; // ) (Shift + 0)

    // 系统键
    m_standardKeyMap[Qt::Key_Pause] = XK_Pause;
    m_standardKeyMap[Qt::Key_Print] = XK_Print;
    m_standardKeyMap[Qt::Key_Help] = XK_Help;

    // ===========================================
    // 小键盘映射 (Numpad Key Mappings)
    // ===========================================
    
    // 小键盘数字键 (0-9)
    m_numpadKeyMap[Qt::Key_0] = XK_KP_0;
    m_numpadKeyMap[Qt::Key_1] = XK_KP_1;
    m_numpadKeyMap[Qt::Key_2] = XK_KP_2;
    m_numpadKeyMap[Qt::Key_3] = XK_KP_3;
    m_numpadKeyMap[Qt::Key_4] = XK_KP_4;
    m_numpadKeyMap[Qt::Key_5] = XK_KP_5;
    m_numpadKeyMap[Qt::Key_6] = XK_KP_6;
    m_numpadKeyMap[Qt::Key_7] = XK_KP_7;
    m_numpadKeyMap[Qt::Key_8] = XK_KP_8;
    m_numpadKeyMap[Qt::Key_9] = XK_KP_9;

    // 小键盘运算符（不受 NumLock 影响，始终有效）
    m_numpadKeyMap[Qt::Key_Asterisk] = XK_KP_Multiply;    // * (小键盘)
    m_numpadKeyMap[Qt::Key_Plus] = XK_KP_Add;             // + (小键盘)
    m_numpadKeyMap[Qt::Key_Minus] = XK_KP_Subtract;       // - (小键盘)
    m_numpadKeyMap[Qt::Key_Period] = XK_KP_Decimal;       // . (小键盘)
    m_numpadKeyMap[Qt::Key_Slash] = XK_KP_Divide;         // / (小键盘)
    m_numpadKeyMap[Qt::Key_Enter] = XK_KP_Enter;          // Enter (小键盘)
    m_numpadKeyMap[Qt::Key_Equal] = XK_KP_Equal;          // = (小键盘)
    
    qCDebug(lcKeyboardSimulatorLinux) << "Key mappings initialized:"
        << "Standard keys:" << m_standardKeyMap.size()
        << ", Numpad keys:" << m_numpadKeyMap.size();
    
    // 调试输出:检查关键按键的映射
    qCDebug(lcKeyboardSimulatorLinux) << "Backspace mapping: Qt::Key_Backspace (" << Qt::Key_Backspace 
        << ") -> KeySym" << Qt::hex << m_standardKeyMap[Qt::Key_Backspace];
    qCDebug(lcKeyboardSimulatorLinux) << "Delete mapping: Qt::Key_Delete (" << Qt::Key_Delete 
        << ") -> KeySym" << Qt::hex << m_standardKeyMap[Qt::Key_Delete];
    qCDebug(lcKeyboardSimulatorLinux) << "Return mapping: Qt::Key_Return (" << Qt::Key_Return 
        << ") -> KeySym" << Qt::hex << m_standardKeyMap[Qt::Key_Return];
}

#endif // Q_OS_LINUX
