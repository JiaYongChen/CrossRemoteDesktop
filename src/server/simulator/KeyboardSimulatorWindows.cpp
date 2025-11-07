#include "KeyboardSimulatorWindows.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcKeyboardSimulatorWindows, "simulator.keyboard.windows")

KeyboardSimulatorWindows::KeyboardSimulatorWindows() : KeyboardSimulator() {
    initializeKeyMappings();
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
    qDebug() << "Standard key mappings:" << m_standardKeyMap.size();
    qDebug() << "Numpad key mappings:" << m_numpadKeyMap.size();
    return true;
}

void KeyboardSimulatorWindows::cleanup() {
    // Windows API 不需要特殊清理
    m_initialized = false;
}

bool KeyboardSimulatorWindows::simulateKeyPress(int qtKey, Qt::KeyboardModifiers modifiers) {
    if (!m_initialized || !m_enabled) {
        qCDebug(lcKeyboardSimulatorWindows) << "simulateKeyPress: Not initialized or enabled";
        return false;
    }

    if (!isValidKey(qtKey)) {
        setLastError("Invalid key code");
        return false;
    }

    qCDebug(lcKeyboardSimulatorWindows) << "simulateKeyPress: qtKey=" << Qt::hex << qtKey 
        << "(" << Qt::dec << qtKey << "), modifiers=" << modifiers;

    WORD winKey = qtKeyToWindowsKey(qtKey);
    DWORD winModifiers = qtModifiersToWindowsModifiers(modifiers);
    
    qCDebug(lcKeyboardSimulatorWindows) << "Mapped to winKey=" << Qt::hex << winKey 
        << "(" << Qt::dec << winKey << ")";
    
    return simulateKeyboardEvent(winKey, 0, winModifiers); // 0 表示按下
}

bool KeyboardSimulatorWindows::simulateKeyRelease(int qtKey, Qt::KeyboardModifiers modifiers) {
    if (!m_initialized || !m_enabled) {
        qCDebug(lcKeyboardSimulatorWindows) << "simulateKeyRelease: Not initialized or enabled";
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

    // 检测主键是否是修饰键本身
    bool isMainKeyModifier = (key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU || 
                              key == VK_LWIN || key == VK_RWIN);

    if (isMainKeyModifier) {
        qCDebug(lcKeyboardSimulatorWindows) << "Main key is a modifier key, will send it directly";
    }

    // 重要说明：
    // Qt 会为每个按键（包括修饰键）发送独立的 keyPress/keyRelease 事件
    // 例如按下 Ctrl+C 的完整序列：
    //   1. keyPress(Ctrl) -> modifiers=ControlModifier
    //   2. keyPress(C) -> modifiers=ControlModifier
    //   3. keyRelease(C) -> modifiers=ControlModifier
    //   4. keyRelease(Ctrl) -> modifiers=NoModifier
    //
    // 因此，我们不应该在普通键的事件中自动添加/移除修饰键
    // 修饰键应该完全通过它们自己的独立事件来处理
    //
    // 这里的 modifiers 参数仅用于调试和日志记录，
    // 实际的修饰键状态由操作系统根据之前收到的修饰键事件来维护

    // 只发送主键事件，不自动处理修饰键
    INPUT mainInput = {0};
    mainInput.type = INPUT_KEYBOARD;
    mainInput.ki.wVk = key;
    mainInput.ki.dwFlags = flags;
    inputs.push_back(mainInput);

    UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent == inputs.size()) {
        qCDebug(lcKeyboardSimulatorWindows) << "Keyboard event simulated: key=" << key 
            << "flags=" << flags << "modifiers=" << modifiers 
            << "(modifiers are for reference only)";
        return true;
    }

    qCWarning(lcKeyboardSimulatorWindows) << "Failed to send keyboard input: key=" << key;
    return false;
}

WORD KeyboardSimulatorWindows::qtKeyToWindowsKey(int qtKey) const {
    // 检测是否是小键盘按键 (Qt::KeypadModifier = 0x20000000)
    bool isKeypad = (qtKey & 0x20000000) != 0;
    int baseKey = qtKey & ~0x20000000;  // 移除 KeypadModifier 标志
    
    qCDebug(lcKeyboardSimulatorWindows) << "qtKeyToWindowsKey: qtKey=" << Qt::hex << qtKey 
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

WORD KeyboardSimulatorWindows::handleNumpadKey(int baseKey, int originalKey) const {
    qCDebug(lcKeyboardSimulatorWindows) << "Processing numpad key: baseKey=" << Qt::hex << baseKey 
        << ", originalKey=" << originalKey;
    
    // 步骤 1: 在小键盘专用映射表中查找
    auto it = m_numpadKeyMap.find(baseKey);
    if (it != m_numpadKeyMap.end()) {
        qCDebug(lcKeyboardSimulatorWindows) << "Found in numpad map: baseKey=" << Qt::hex << baseKey
            << "-> VK=" << Qt::hex << it->second << "(VK_NUMPAD*)";
        return it->second;
    }
    
    // 步骤 2: 小键盘映射表中未找到，检查是否是导航键
    // NumLock 关闭时，小键盘会发送 Insert/Delete/Home/End/PageUp/PageDown/Left/Right/Up/Down/Clear
    // 这些键应该映射到标准的导航键 VK 码
    qCDebug(lcKeyboardSimulatorWindows) << "Not found in numpad map, checking if it's a navigation key";
    
    auto stdIt = m_standardKeyMap.find(baseKey);
    if (stdIt != m_standardKeyMap.end()) {
        qCDebug(lcKeyboardSimulatorWindows) << "Found navigation key in standard map: baseKey=" << Qt::hex << baseKey
            << "-> VK=" << Qt::hex << stdIt->second;
        return stdIt->second;
    }
    
    // 步骤 3: 仍未找到映射，记录警告并返回默认值
    qCWarning(lcKeyboardSimulatorWindows) << "Unmapped numpad key:" << Qt::hex << originalKey 
        << "(baseKey=" << baseKey << "), using fallback";
    return static_cast<WORD>(baseKey & 0xFFFF);
}

WORD KeyboardSimulatorWindows::handleStandardKey(int qtKey) const {
    qCDebug(lcKeyboardSimulatorWindows) << "Processing standard keyboard key: qtKey=" << Qt::hex << qtKey;
    
    // 在标准键盘映射表中查找
    auto it = m_standardKeyMap.find(qtKey);
    if (it != m_standardKeyMap.end()) {
        qCDebug(lcKeyboardSimulatorWindows) << "Found in standard map: qtKey=" << Qt::hex << qtKey 
            << "-> VK=" << Qt::hex << it->second << "(" << Qt::dec << it->second << ")";
        return it->second;
    }
    
    // 未找到映射，记录警告并返回默认值
    qCWarning(lcKeyboardSimulatorWindows) << "Unmapped standard key:" << Qt::hex << qtKey 
        << "(" << Qt::dec << qtKey << "), using fallback VK=" << (qtKey & 0xFFFF);
    return static_cast<WORD>(qtKey & 0xFFFF);
}

DWORD KeyboardSimulatorWindows::qtModifiersToWindowsModifiers(Qt::KeyboardModifiers modifiers) const {
    DWORD result = 0;
    
    // 过滤掉 KeypadModifier，它不是真正的修饰键，只是用来标识小键盘的标志
    // KeypadModifier = 0x20000000，不应该被转换为 Windows 修饰键
    Qt::KeyboardModifiers filteredModifiers = modifiers & ~Qt::KeypadModifier;
    
    if (filteredModifiers & Qt::ControlModifier) result |= 0x0002;
    if (filteredModifiers & Qt::ShiftModifier) result |= 0x0004;
    if (filteredModifiers & Qt::AltModifier) result |= 0x0001;
    
    qCDebug(lcKeyboardSimulatorWindows) << "Modifiers conversion: Qt=" << Qt::hex << modifiers 
        << "filtered=" << filteredModifiers 
        << "-> Windows=" << result 
        << "(Ctrl=" << bool(result & 0x0002) 
        << ", Shift=" << bool(result & 0x0004) 
        << ", Alt=" << bool(result & 0x0001) << ")";
    
    return result;
}

void KeyboardSimulatorWindows::initializeKeyMappings() {
    // ===========================================
    // 标准按键映射 (Standard Key Mappings)
    // ===========================================
    
    // 字母键 A-Z (VK: 0x41-0x5A)
    m_standardKeyMap[Qt::Key_A] = 0x41;
    m_standardKeyMap[Qt::Key_B] = 0x42;
    m_standardKeyMap[Qt::Key_C] = 0x43;
    m_standardKeyMap[Qt::Key_D] = 0x44;
    m_standardKeyMap[Qt::Key_E] = 0x45;
    m_standardKeyMap[Qt::Key_F] = 0x46;
    m_standardKeyMap[Qt::Key_G] = 0x47;
    m_standardKeyMap[Qt::Key_H] = 0x48;
    m_standardKeyMap[Qt::Key_I] = 0x49;
    m_standardKeyMap[Qt::Key_J] = 0x4A;
    m_standardKeyMap[Qt::Key_K] = 0x4B;
    m_standardKeyMap[Qt::Key_L] = 0x4C;
    m_standardKeyMap[Qt::Key_M] = 0x4D;
    m_standardKeyMap[Qt::Key_N] = 0x4E;
    m_standardKeyMap[Qt::Key_O] = 0x4F;
    m_standardKeyMap[Qt::Key_P] = 0x50;
    m_standardKeyMap[Qt::Key_Q] = 0x51;
    m_standardKeyMap[Qt::Key_R] = 0x52;
    m_standardKeyMap[Qt::Key_S] = 0x53;
    m_standardKeyMap[Qt::Key_T] = 0x54;
    m_standardKeyMap[Qt::Key_U] = 0x55;
    m_standardKeyMap[Qt::Key_V] = 0x56;
    m_standardKeyMap[Qt::Key_W] = 0x57;
    m_standardKeyMap[Qt::Key_X] = 0x58;
    m_standardKeyMap[Qt::Key_Y] = 0x59;
    m_standardKeyMap[Qt::Key_Z] = 0x5A;

    // 主键盘数字键 0-9 (VK: 0x30-0x39)
    m_standardKeyMap[Qt::Key_0] = 0x30;
    m_standardKeyMap[Qt::Key_1] = 0x31;
    m_standardKeyMap[Qt::Key_2] = 0x32;
    m_standardKeyMap[Qt::Key_3] = 0x33;
    m_standardKeyMap[Qt::Key_4] = 0x34;
    m_standardKeyMap[Qt::Key_5] = 0x35;
    m_standardKeyMap[Qt::Key_6] = 0x36;
    m_standardKeyMap[Qt::Key_7] = 0x37;
    m_standardKeyMap[Qt::Key_8] = 0x38;
    m_standardKeyMap[Qt::Key_9] = 0x39;

    // 功能键 F1-F24 (VK: VK_F1-VK_F24)
    m_standardKeyMap[Qt::Key_F1] = VK_F1;
    m_standardKeyMap[Qt::Key_F2] = VK_F2;
    m_standardKeyMap[Qt::Key_F3] = VK_F3;
    m_standardKeyMap[Qt::Key_F4] = VK_F4;
    m_standardKeyMap[Qt::Key_F5] = VK_F5;
    m_standardKeyMap[Qt::Key_F6] = VK_F6;
    m_standardKeyMap[Qt::Key_F7] = VK_F7;
    m_standardKeyMap[Qt::Key_F8] = VK_F8;
    m_standardKeyMap[Qt::Key_F9] = VK_F9;
    m_standardKeyMap[Qt::Key_F10] = VK_F10;
    m_standardKeyMap[Qt::Key_F11] = VK_F11;
    m_standardKeyMap[Qt::Key_F12] = VK_F12;
    m_standardKeyMap[Qt::Key_F13] = VK_F13;
    m_standardKeyMap[Qt::Key_F14] = VK_F14;
    m_standardKeyMap[Qt::Key_F15] = VK_F15;
    m_standardKeyMap[Qt::Key_F16] = VK_F16;
    m_standardKeyMap[Qt::Key_F17] = VK_F17;
    m_standardKeyMap[Qt::Key_F18] = VK_F18;
    m_standardKeyMap[Qt::Key_F19] = VK_F19;
    m_standardKeyMap[Qt::Key_F20] = VK_F20;
    m_standardKeyMap[Qt::Key_F21] = VK_F21;
    m_standardKeyMap[Qt::Key_F22] = VK_F22;
    m_standardKeyMap[Qt::Key_F23] = VK_F23;
    m_standardKeyMap[Qt::Key_F24] = VK_F24;

    // 控制键
    m_standardKeyMap[Qt::Key_Return] = VK_RETURN;
    m_standardKeyMap[Qt::Key_Enter] = VK_RETURN;
    m_standardKeyMap[Qt::Key_Tab] = VK_TAB;
    m_standardKeyMap[Qt::Key_Space] = VK_SPACE;
    m_standardKeyMap[Qt::Key_Backspace] = VK_BACK;
    m_standardKeyMap[Qt::Key_Delete] = VK_DELETE;
    m_standardKeyMap[Qt::Key_Escape] = VK_ESCAPE;
    m_standardKeyMap[Qt::Key_Insert] = VK_INSERT;
    m_standardKeyMap[Qt::Key_Home] = VK_HOME;
    m_standardKeyMap[Qt::Key_End] = VK_END;
    m_standardKeyMap[Qt::Key_PageUp] = VK_PRIOR;
    m_standardKeyMap[Qt::Key_PageDown] = VK_NEXT;

    // 方向键
    m_standardKeyMap[Qt::Key_Left] = VK_LEFT;
    m_standardKeyMap[Qt::Key_Right] = VK_RIGHT;
    m_standardKeyMap[Qt::Key_Up] = VK_UP;
    m_standardKeyMap[Qt::Key_Down] = VK_DOWN;

    // 修饰键
    m_standardKeyMap[Qt::Key_Shift] = VK_SHIFT;
    m_standardKeyMap[Qt::Key_Control] = VK_CONTROL;
    m_standardKeyMap[Qt::Key_Alt] = VK_MENU;
    m_standardKeyMap[Qt::Key_Meta] = VK_LWIN;      // Windows 键
    m_standardKeyMap[Qt::Key_AltGr] = VK_RMENU;    // Right Alt

    // 锁定键
    m_standardKeyMap[Qt::Key_CapsLock] = VK_CAPITAL;
    m_standardKeyMap[Qt::Key_NumLock] = VK_NUMLOCK;
    m_standardKeyMap[Qt::Key_ScrollLock] = VK_SCROLL;

    // 符号键 (OEM Keys)
    // 重要说明：某些符号键（如 +、-、*、/、.）在主键盘和小键盘上都存在
    // Qt 通过 KeypadModifier (0x20000000) 标志来区分它们：
    //   - 无 KeypadModifier → handleStandardKey() → 查找 m_standardKeyMap
    //   - 有 KeypadModifier → handleNumpadKey() → 查找 m_numpadKeyMap
    // 
    // 处理流程：
    //   1. 主键盘的 '+' (Shift+=) → Qt::Key_Plus(无KeypadModifier) → VK_OEM_PLUS
    //   2. 小键盘的 '+' → Qt::Key_Plus(有KeypadModifier) → VK_ADD
    //   3. 两个映射表中可以有相同的 Qt::Key，通过 KeypadModifier 区分
    
    m_standardKeyMap[Qt::Key_Semicolon] = VK_OEM_1;      // ;:
    m_standardKeyMap[Qt::Key_Plus] = VK_OEM_PLUS;        // =+ (主键盘)
    m_standardKeyMap[Qt::Key_Comma] = VK_OEM_COMMA;      // ,<
    m_standardKeyMap[Qt::Key_Minus] = VK_OEM_MINUS;      // -_ (主键盘)
    m_standardKeyMap[Qt::Key_Period] = VK_OEM_PERIOD;    // .> (主键盘)
    m_standardKeyMap[Qt::Key_Slash] = VK_OEM_2;          // /? (主键盘)
    m_standardKeyMap[Qt::Key_AsciiTilde] = VK_OEM_3;     // `~
    m_standardKeyMap[Qt::Key_BracketLeft] = VK_OEM_4;    // [{
    m_standardKeyMap[Qt::Key_Backslash] = VK_OEM_5;      // \|
    m_standardKeyMap[Qt::Key_BracketRight] = VK_OEM_6;   // ]}
    m_standardKeyMap[Qt::Key_Apostrophe] = VK_OEM_7;     // '"
    m_standardKeyMap[Qt::Key_QuoteLeft] = VK_OEM_3;      // ` (同 AsciiTilde)
    m_standardKeyMap[Qt::Key_Equal] = VK_OEM_PLUS;       // = (物理上与 + 是同一个键)
    m_standardKeyMap[Qt::Key_Underscore] = VK_OEM_MINUS; // _ (物理上与 - 是同一个键)
    m_standardKeyMap[Qt::Key_Less] = VK_OEM_COMMA;       // < (物理上与 , 是同一个键)
    m_standardKeyMap[Qt::Key_Greater] = VK_OEM_PERIOD;   // > (物理上与 . 是同一个键)
    m_standardKeyMap[Qt::Key_Question] = VK_OEM_2;       // ? (物理上与 / 是同一个键)
    m_standardKeyMap[Qt::Key_Colon] = VK_OEM_1;          // : (物理上与 ; 是同一个键)

    // 系统键
    m_standardKeyMap[Qt::Key_Pause] = VK_PAUSE;
    m_standardKeyMap[Qt::Key_Print] = VK_SNAPSHOT;
    m_standardKeyMap[Qt::Key_Help] = VK_HELP;
    m_standardKeyMap[Qt::Key_Clear] = VK_CLEAR;
    m_standardKeyMap[Qt::Key_Select] = VK_SELECT;
    m_standardKeyMap[Qt::Key_Execute] = VK_EXECUTE;

    // 媒体键
    m_standardKeyMap[Qt::Key_VolumeUp] = VK_VOLUME_UP;
    m_standardKeyMap[Qt::Key_VolumeDown] = VK_VOLUME_DOWN;
    m_standardKeyMap[Qt::Key_VolumeMute] = VK_VOLUME_MUTE;
    m_standardKeyMap[Qt::Key_MediaPlay] = VK_MEDIA_PLAY_PAUSE;
    m_standardKeyMap[Qt::Key_MediaStop] = VK_MEDIA_STOP;
    m_standardKeyMap[Qt::Key_MediaPrevious] = VK_MEDIA_PREV_TRACK;
    m_standardKeyMap[Qt::Key_MediaNext] = VK_MEDIA_NEXT_TRACK;

    // 浏览器键
    m_standardKeyMap[Qt::Key_Back] = VK_BROWSER_BACK;
    m_standardKeyMap[Qt::Key_Forward] = VK_BROWSER_FORWARD;
    m_standardKeyMap[Qt::Key_Refresh] = VK_BROWSER_REFRESH;
    m_standardKeyMap[Qt::Key_Stop] = VK_BROWSER_STOP;
    m_standardKeyMap[Qt::Key_Search] = VK_BROWSER_SEARCH;
    m_standardKeyMap[Qt::Key_Favorites] = VK_BROWSER_FAVORITES;
    m_standardKeyMap[Qt::Key_HomePage] = VK_BROWSER_HOME;

    // 应用程序键
    m_standardKeyMap[Qt::Key_LaunchMail] = VK_LAUNCH_MAIL;
    m_standardKeyMap[Qt::Key_LaunchMedia] = VK_LAUNCH_MEDIA_SELECT;
    
    // ===========================================
    // 小键盘映射 (Numpad Key Mappings)
    // ===========================================
    // 
    // 小键盘按键处理逻辑：
    //   - 所有小键盘按键都带有 KeypadModifier (0x20000000) 标志
    //   - handleNumpadKey() 会移除此标志后在此映射表中查找
    //   - NumLock 开启：数字键 0-9 和运算符有效
    //   - NumLock 关闭：导航键（Insert/Delete/Home/End等）生效
    //     这些导航键会回退到 m_standardKeyMap 查找对应的 VK 码
    
    // 小键盘数字键 (VK: VK_NUMPAD0-VK_NUMPAD9)
    // 仅在 NumLock 开启时，Qt 才会发送这些键码
    m_numpadKeyMap[Qt::Key_0] = VK_NUMPAD0;
    m_numpadKeyMap[Qt::Key_1] = VK_NUMPAD1;
    m_numpadKeyMap[Qt::Key_2] = VK_NUMPAD2;
    m_numpadKeyMap[Qt::Key_3] = VK_NUMPAD3;
    m_numpadKeyMap[Qt::Key_4] = VK_NUMPAD4;
    m_numpadKeyMap[Qt::Key_5] = VK_NUMPAD5;
    m_numpadKeyMap[Qt::Key_6] = VK_NUMPAD6;
    m_numpadKeyMap[Qt::Key_7] = VK_NUMPAD7;
    m_numpadKeyMap[Qt::Key_8] = VK_NUMPAD8;
    m_numpadKeyMap[Qt::Key_9] = VK_NUMPAD9;

    // 小键盘运算符（不受 NumLock 影响，始终有效）
    // 注意：这些 Qt::Key 在 m_standardKeyMap 中也存在，通过 KeypadModifier 区分
    m_numpadKeyMap[Qt::Key_Asterisk] = VK_MULTIPLY;  // * (小键盘专用 VK 码)
    m_numpadKeyMap[Qt::Key_Plus] = VK_ADD;           // + (小键盘专用 VK 码)
    m_numpadKeyMap[Qt::Key_Minus] = VK_SUBTRACT;     // - (小键盘专用 VK 码)
    m_numpadKeyMap[Qt::Key_Period] = VK_DECIMAL;     // . (小键盘专用 VK 码)
    m_numpadKeyMap[Qt::Key_Slash] = VK_DIVIDE;       // / (小键盘专用 VK 码)
    m_numpadKeyMap[Qt::Key_Enter] = VK_RETURN;       // Enter (与主键盘 Enter 共用 VK 码)
    
    qCDebug(lcKeyboardSimulatorWindows) << "Key mappings initialized:"
        << "Standard keys:" << m_standardKeyMap.size()
        << ", Numpad keys:" << m_numpadKeyMap.size();
    
    // 调试输出:检查关键按键的映射
    qCDebug(lcKeyboardSimulatorWindows) << "Backspace mapping: Qt::Key_Backspace (" << Qt::Key_Backspace 
        << ") -> VK" << Qt::hex << m_standardKeyMap[Qt::Key_Backspace];
    qCDebug(lcKeyboardSimulatorWindows) << "Delete mapping: Qt::Key_Delete (" << Qt::Key_Delete 
        << ") -> VK" << Qt::hex << m_standardKeyMap[Qt::Key_Delete];
    qCDebug(lcKeyboardSimulatorWindows) << "Return mapping: Qt::Key_Return (" << Qt::Key_Return 
        << ") -> VK" << Qt::hex << m_standardKeyMap[Qt::Key_Return];
}

#endif // Q_OS_WIN
