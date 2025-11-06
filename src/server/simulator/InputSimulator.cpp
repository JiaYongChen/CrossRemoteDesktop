#include "InputSimulator.h"
#include "../../common/core/config/Constants.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtWidgets/QApplication>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QDebug>
#include <QtCore/QLoggingCategory>
#include <QtGui/QCursor>
#include <vector>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <Winuser.h>
#elif defined(Q_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#elif defined(Q_OS_LINUX)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xtest.h>
#include <X11/Keysym.h>
#include <X11/Xkblib.h>
#endif

InputSimulator::InputSimulator(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_enabled(true)
    , m_batchMode(false)
    , m_mouseSpeed(CoreConstants::Input::DEFAULT_MOUSE_SPEED)
    , m_keyboardDelay(CoreConstants::Input::DEFAULT_KEYBOARD_DELAY)
    , m_mouseDelay(CoreConstants::Input::DEFAULT_MOUSE_DELAY)
#ifdef Q_OS_LINUX
    , m_display(nullptr)
#endif
{
    initialize();
}

InputSimulator::~InputSimulator() {
    cleanup();
}

bool InputSimulator::initialize() {
    if ( m_initialized ) {
        return true;
    }

#ifdef Q_OS_WIN
    m_initialized = initializeWindows();
#elif defined(Q_OS_MACOS)
    m_initialized = initializeMacOS();
#elif defined(Q_OS_LINUX)
    m_initialized = initializeLinux();
#else
    m_initialized = true; // 其他平台默认初始化成功
#endif

    if ( m_initialized ) {
        m_screenSize = getScreenSize();
    }

    return m_initialized;
}

void InputSimulator::cleanup() {
    if ( !m_initialized ) {
        return;
    }

#ifdef Q_OS_WIN
    cleanupWindows();
#elif defined(Q_OS_MACOS)
    cleanupMacOS();
#elif defined(Q_OS_LINUX)
    cleanupLinux();
#endif

    m_initialized = false;
}

bool InputSimulator::isInitialized() const {
    return m_initialized;
}

bool InputSimulator::simulateMouseMove(int x, int y) {
    if ( !m_initialized || !m_enabled ) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }

    if ( !isValidCoordinate(x, y) ) {
        setLastError("Invalid coordinates");
        return false;
    }

    bool result = false;

#ifdef Q_OS_WIN
    result = simulateMouseWindows(x, y, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE);
#elif defined(Q_OS_MACOS)
    // macOS 鼠标移动需要明确指定按钮参数（kCGMouseButtonLeft 用于移动）
    result = simulateMouseMacOS(x, y, kCGEventMouseMoved, kCGMouseButtonLeft);
#elif defined(Q_OS_LINUX)
    result = simulateMouseLinux(x, y, 0, false);
#endif

    if ( result ) {
        emit mouseSimulated(x, y, Qt::NoButton, "move");
    } else {
        qCWarning(lcInputSimulator) << "Failed to simulate mouse move to" << x << y;
    }

    return result;
}

bool InputSimulator::simulateMousePress(int x, int y, Qt::MouseButton button) {
    if ( !m_initialized || !m_enabled ) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }

    if ( !isValidCoordinate(x, y) ) {
        setLastError("Invalid coordinates");
        return false;
    }

    bool result = false;

#ifdef Q_OS_WIN
    DWORD flags = MOUSEEVENTF_ABSOLUTE;
    if ( button == Qt::LeftButton ) flags |= MOUSEEVENTF_LEFTDOWN;
    else if ( button == Qt::RightButton ) flags |= MOUSEEVENTF_RIGHTDOWN;
    else if ( button == Qt::MiddleButton ) flags |= MOUSEEVENTF_MIDDLEDOWN;
    result = simulateMouseWindows(x, y, flags);
#elif defined(Q_OS_MACOS)
    CGEventType eventType = kCGEventLeftMouseDown;
    CGMouseButton cgButton = kCGMouseButtonLeft;
    if ( button == Qt::RightButton ) {
        eventType = kCGEventRightMouseDown;
        cgButton = kCGMouseButtonRight;
    } else if ( button == Qt::MiddleButton ) {
        eventType = kCGEventOtherMouseDown;
        cgButton = kCGMouseButtonCenter;
    }
    result = simulateMouseMacOS(x, y, eventType, cgButton);
#elif defined(Q_OS_LINUX)
    unsigned int linuxButton = 1;
    if ( button == Qt::RightButton ) linuxButton = 3;
    else if ( button == Qt::MiddleButton ) linuxButton = 2;
    result = simulateMouseLinux(x, y, linuxButton, true);
#endif

    if ( result ) {
        emit mouseSimulated(x, y, button, "press");
    } else {
        qCWarning(lcInputSimulator) << "Failed to simulate mouse press at" << x << y << "button:" << button;
    }

    return result;
}

bool InputSimulator::simulateMouseRelease(int x, int y, Qt::MouseButton button) {
    if ( !m_initialized || !m_enabled ) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }

    if ( !isValidCoordinate(x, y) ) {
        setLastError("Invalid coordinates");
        return false;
    }

    bool result = false;

#ifdef Q_OS_WIN
    DWORD flags = MOUSEEVENTF_ABSOLUTE;
    if ( button == Qt::LeftButton ) flags |= MOUSEEVENTF_LEFTUP;
    else if ( button == Qt::RightButton ) flags |= MOUSEEVENTF_RIGHTUP;
    else if ( button == Qt::MiddleButton ) flags |= MOUSEEVENTF_MIDDLEUP;
    result = simulateMouseWindows(x, y, flags);
#elif defined(Q_OS_MACOS)
    CGEventType eventType = kCGEventLeftMouseUp;
    CGMouseButton cgButton = kCGMouseButtonLeft;
    if ( button == Qt::RightButton ) {
        eventType = kCGEventRightMouseUp;
        cgButton = kCGMouseButtonRight;
    } else if ( button == Qt::MiddleButton ) {
        eventType = kCGEventOtherMouseUp;
        cgButton = kCGMouseButtonCenter;
    }
    result = simulateMouseMacOS(x, y, eventType, cgButton);
#elif defined(Q_OS_LINUX)
    unsigned int linuxButton = 1;
    if ( button == Qt::RightButton ) linuxButton = 3;
    else if ( button == Qt::MiddleButton ) linuxButton = 2;
    result = simulateMouseLinux(x, y, linuxButton, false);
#endif

    if ( result ) {
        emit mouseSimulated(x, y, button, "release");
    } else {
        qCWarning(lcInputSimulator) << "Failed to simulate mouse release at" << x << y << "button:" << button;
    }

    return result;
}

bool InputSimulator::simulateMouseClick(int x, int y, Qt::MouseButton button) {
    bool pressResult = simulateMousePress(x, y, button);
    delay(m_mouseDelay);
    bool releaseResult = simulateMouseRelease(x, y, button);

    return pressResult && releaseResult;
}

bool InputSimulator::simulateMouseDoubleClick(int x, int y, Qt::MouseButton button) {
    bool firstClick = simulateMouseClick(x, y, button);
    delay(50); // 双击间隔
    bool secondClick = simulateMouseClick(x, y, button);

    return firstClick && secondClick;
}

bool InputSimulator::simulateMouseWheel(int x, int y, int delta) {
    if ( !m_initialized || !m_enabled ) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }

    bool result = false;

#ifdef Q_OS_WIN
    result = simulateMouseWindows(x, y, MOUSEEVENTF_WHEEL, delta);
#elif defined(Q_OS_MACOS)
    // Qt 的滚轮 delta 通常是 120 的倍数
    // 转换为滚动行数: delta / 120
    // 使用 kCGScrollEventUnitLine 更符合系统行为
    int scrollLines = delta / 120;
    if ( scrollLines == 0 && delta != 0 ) {
        scrollLines = (delta > 0) ? 1 : -1;
    }

    CGEventRef event = CGEventCreateScrollWheelEvent(
        nullptr,
        kCGScrollEventUnitLine,  // 使用行单位而不是像素
        1,                       // 滚动轴数量(垂直滚动)
        scrollLines              // 滚动量
    );

    if ( event ) {
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        result = true;
    }
#elif defined(Q_OS_LINUX)
    unsigned int button = (delta > 0) ? 4 : 5;
    result = simulateMouseLinux(x, y, button, true) && simulateMouseLinux(x, y, button, false);
#endif

    if ( result ) {
        emit mouseSimulated(x, y, Qt::NoButton, "wheel");
    }

    return result;
}

bool InputSimulator::simulateKeyPress(int key, Qt::KeyboardModifiers modifiers) {
    if ( !m_initialized || !m_enabled ) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }

    if ( !isValidKey(key) ) {
        setLastError("Invalid key");
        return false;
    }

    bool result = false;

#ifdef Q_OS_WIN
    WORD winKey = qtKeyToWindowsKey(key);
    DWORD winModifiers = qtModifiersToWindowsModifiers(modifiers);
    result = simulateKeyboardWindows(winKey, 0, winModifiers); // Key down
#elif defined(Q_OS_MACOS)
    CGKeyCode macKey = qtKeyToMacOSKey(key);
    CGEventFlags macModifiers = qtModifiersToMacOSModifiers(modifiers);
    result = simulateKeyboardMacOS(macKey, true, macModifiers);
#elif defined(Q_OS_LINUX)
    KeySym linuxKey = qtKeyToLinuxKey(key);
    unsigned int linuxModifiers = qtModifiersToLinuxModifiers(modifiers);
    result = simulateKeyboardLinux(linuxKey, true, linuxModifiers);
#endif

    if ( result ) {
        emit keyboardSimulated(key, modifiers, "press");
    }

    return result;
}

bool InputSimulator::simulateKeyRelease(int key, Qt::KeyboardModifiers modifiers) {
    if ( !m_initialized || !m_enabled ) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }

    if ( !isValidKey(key) ) {
        setLastError("Invalid key");
        return false;
    }

    bool result = false;

#ifdef Q_OS_WIN
    WORD winKey = qtKeyToWindowsKey(key);
    DWORD winModifiers = qtModifiersToWindowsModifiers(modifiers);
    result = simulateKeyboardWindows(winKey, KEYEVENTF_KEYUP, winModifiers);
#elif defined(Q_OS_MACOS)
    CGKeyCode macKey = qtKeyToMacOSKey(key);
    CGEventFlags macModifiers = qtModifiersToMacOSModifiers(modifiers);
    result = simulateKeyboardMacOS(macKey, false, macModifiers);
#elif defined(Q_OS_LINUX)
    KeySym linuxKey = qtKeyToLinuxKey(key);
    unsigned int linuxModifiers = qtModifiersToLinuxModifiers(modifiers);
    result = simulateKeyboardLinux(linuxKey, false, linuxModifiers);
#endif

    if ( result ) {
        emit keyboardSimulated(key, modifiers, "release");
    }

    return result;
}bool InputSimulator::simulateKeyClick(int key, Qt::KeyboardModifiers modifiers) {
    bool pressResult = simulateKeyPress(key, modifiers);
    delay(m_keyboardDelay);
    bool releaseResult = simulateKeyRelease(key, modifiers);

    return pressResult && releaseResult;
}

bool InputSimulator::simulateTextInput(const QString& text) {
    for ( const QChar& ch : text ) {
        int key = ch.unicode();
        if ( !simulateKeyClick(key) ) {
            return false;
        }
        delay(m_keyboardDelay);
    }
    return true;
}

bool InputSimulator::simulateKeySequence(const QList<int>& keys, Qt::KeyboardModifiers modifiers) {
    for ( int key : keys ) {
        if ( !simulateKeyClick(key, modifiers) ) {
            return false;
        }
        delay(m_keyboardDelay);
    }
    return true;
}

bool InputSimulator::simulateShortcut(Qt::Key key, Qt::KeyboardModifiers modifiers) {
    return simulateKeyClick(static_cast<int>(key), modifiers);
}

QSize InputSimulator::getScreenSize() const {
#ifdef Q_OS_WIN
    return QSize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
#elif defined(Q_OS_MACOS)
    CGRect screenRect = CGDisplayBounds(CGMainDisplayID());
    return QSize(static_cast<int>(screenRect.size.width), static_cast<int>(screenRect.size.height));
#elif defined(Q_OS_LINUX)
    if ( m_display ) {
        Screen* screen = DefaultScreenOfDisplay(m_display);
        return QSize(WidthOfScreen(screen), HeightOfScreen(screen));
}
#endif
    return QSize(1920, 1080); // 默认分辨率
}

QPoint InputSimulator::getCursorPosition() const {
#ifdef Q_OS_WIN
    POINT point;
    if ( GetCursorPos(&point) ) {
        return QPoint(point.x, point.y);
}
#elif defined(Q_OS_MACOS)
    CGEventRef event = CGEventCreate(nullptr);
    if ( event ) {
        CGPoint point = CGEventGetLocation(event);
        CFRelease(event);
        return QPoint(static_cast<int>(point.x), static_cast<int>(point.y));
    }
#elif defined(Q_OS_LINUX)
    if ( m_display ) {
        Window root, child;
        int rootX, rootY, winX, winY;
        unsigned int mask;
        if ( XQueryPointer(m_display, DefaultRootWindow(m_display), &root, &child, &rootX, &rootY, &winX, &winY, &mask) ) {
            return QPoint(rootX, rootY);
        }
    }
#endif
    return QPoint();
}

bool InputSimulator::setCursorPosition(const QPoint& position) {
    return simulateMouseMove(position.x(), position.y());
}

void InputSimulator::setMouseSpeed(int speed) {
    m_mouseSpeed = qBound(1, speed, 10);
}

int InputSimulator::mouseSpeed() const {
    return m_mouseSpeed;
}

void InputSimulator::setKeyboardDelay(int msecs) {
    m_keyboardDelay = qMax(0, msecs);
}

int InputSimulator::keyboardDelay() const {
    return m_keyboardDelay;
}

void InputSimulator::setMouseDelay(int msecs) {
    m_mouseDelay = qMax(0, msecs);
}

int InputSimulator::mouseDelay() const {
    return m_mouseDelay;
}

void InputSimulator::setEnabled(bool enabled) {
    m_enabled = enabled;
}

bool InputSimulator::isEnabled() const {
    return m_enabled;
}

void InputSimulator::beginBatch() {
    QMutexLocker locker(&m_mutex);
    m_batchMode = true;
}

void InputSimulator::endBatch() {
    QMutexLocker locker(&m_mutex);
    m_batchMode = false;

    // 处理批量操作
    while ( !m_operationQueue.isEmpty() ) {
        InputOperation op = m_operationQueue.dequeue();
        // 执行操作
        switch ( op.type ) {
            case InputOperation::MouseMove:
                simulateMouseMove(op.position.x(), op.position.y());
                break;
            case InputOperation::MousePress:
                simulateMousePress(op.position.x(), op.position.y(), op.mouseButton);
                break;
            case InputOperation::MouseRelease:
                simulateMouseRelease(op.position.x(), op.position.y(), op.mouseButton);
                break;
            case InputOperation::KeyPress:
                simulateKeyPress(op.key, op.modifiers);
                break;
            case InputOperation::KeyRelease:
                simulateKeyRelease(op.key, op.modifiers);
                break;
            case InputOperation::TextInput:
                simulateTextInput(op.text);
                break;
            case InputOperation::Delay:
                delay(op.delayMs);
                break;
        }
    }
}

bool InputSimulator::isBatchMode() const {
    return m_batchMode;
}

QString InputSimulator::lastError() const {
    return m_lastError;
}

void InputSimulator::simulateInput() {
    // 处理输入队列
}

void InputSimulator::setLastError(const QString& error) {
    m_lastError = error;
    if ( !error.isEmpty() ) {
        emit errorOccurred(error);
    }
}

void InputSimulator::delay(int msecs) {
    if ( msecs > 0 ) {
        QThread::msleep(msecs);
    }
}

bool InputSimulator::isValidCoordinate(int x, int y) const {
    return x >= 0 && y >= 0 && x < m_screenSize.width() && y < m_screenSize.height();
}

bool InputSimulator::isValidKey(int key) const {
    return key > 0 && key <= CoreConstants::Input::MAX_KEY_VALUE;
}

QPoint InputSimulator::transformCoordinates(const QPoint& point) const {
    // 平台相关的坐标变换
#ifdef Q_OS_MACOS
    // CoreGraphics 的坐标系原点在屏幕左下，Qt 的坐标系原点在左上
    // 需要将 y 翻转为 macOS 全局显示坐标
    int x = point.x();
    int y = point.y();
    int screenH = m_screenSize.height();
    // 防止越界
    if ( screenH <= 0 ) return point;
    int flippedY = screenH - y;
    return QPoint(x, flippedY);
#else
    // 其他平台当前不需要转换
    return point;
#endif
}

#ifdef Q_OS_WIN
bool InputSimulator::initializeWindows() {
    return true; // Windows API 不需要特殊初始化
}

void InputSimulator::cleanupWindows() {
    // Windows API 不需要特殊清理
}

bool InputSimulator::simulateMouseWindows(int x, int y, DWORD flags, DWORD data) {
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dx = (x * 65535) / GetSystemMetrics(SM_CXSCREEN);
    input.mi.dy = (y * 65535) / GetSystemMetrics(SM_CYSCREEN);
    input.mi.dwFlags = flags;
    input.mi.mouseData = data;

    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool InputSimulator::simulateKeyboardWindows(WORD key, DWORD flags, DWORD modifiers) {
    std::vector<INPUT> inputs;

    // 按下修饰键
    if ( modifiers & 0x0002 ) {  // Ctrl
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_CONTROL;
        input.ki.dwFlags = 0;
        inputs.push_back(input);
    }
    if ( modifiers & 0x0004 ) {  // Shift
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_SHIFT;
        input.ki.dwFlags = 0;
        inputs.push_back(input);
    }
    if ( modifiers & 0x0001 ) {  // Alt
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_MENU;
        input.ki.dwFlags = 0;
        inputs.push_back(input);
    }

    // 主键事件
    INPUT mainInput = { 0 };
    mainInput.type = INPUT_KEYBOARD;
    mainInput.ki.wVk = key;
    mainInput.ki.dwFlags = flags;
    inputs.push_back(mainInput);

    // 释放修饰键（仅在按键释放时）
    if ( flags & KEYEVENTF_KEYUP ) {
        if ( modifiers & 0x0001 ) {  // Alt
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_MENU;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        }
        if ( modifiers & 0x0004 ) {  // Shift
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_SHIFT;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        }
        if ( modifiers & 0x0002 ) {  // Ctrl
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_CONTROL;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        }
    }

    UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    return sent == inputs.size();
}

WORD InputSimulator::qtKeyToWindowsKey(int qtKey) {
    // Qt Key 到 Windows Virtual-Key Code 的映射
    // 参考: https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
    switch ( qtKey ) {
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
            qCDebug(lcInputSimulator) << "Unmapped Qt key:" << qtKey << "using default mapping";
            return static_cast<WORD>(qtKey);
    }
}

DWORD InputSimulator::qtModifiersToWindowsModifiers(Qt::KeyboardModifiers modifiers) {
    DWORD result = 0;
    if ( modifiers & Qt::ControlModifier ) result |= 0x0002;
    if ( modifiers & Qt::ShiftModifier ) result |= 0x0004;
    if ( modifiers & Qt::AltModifier ) result |= 0x0001;
    return result;
}
#endif

#ifdef Q_OS_MACOS
bool InputSimulator::initializeMacOS() {
    // 检查辅助功能权限
    if ( !checkAccessibilityPermission() ) {
        setLastError("需要辅助功能权限才能模拟输入事件。请在系统偏好设置 > 安全性与隐私 > 隐私 > 辅助功能中授予权限。");
        qWarning() << "InputSimulator: 缺少辅助功能权限";

        // 尝试请求权限（会打开系统设置）
        requestAccessibilityPermission();
        return false;
    }

    qDebug() << "InputSimulator: macOS 辅助功能权限已授予";
    return true;
}

void InputSimulator::cleanupMacOS() {
    // macOS API 不需要特殊清理
}

bool InputSimulator::checkAccessibilityPermission() {
    // 检查当前进程是否被信任（有辅助功能权限）
    return AXIsProcessTrusted();
}

bool InputSimulator::requestAccessibilityPermission() {
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

    if ( options ) {
        CFRelease(options);
    }

    return trusted;
}
#endif

#ifdef Q_OS_MACOS

bool InputSimulator::simulateMouseMacOS(int x, int y, CGEventType eventType, CGMouseButton button) {
    // 检查辅助功能权限
    if ( !AXIsProcessTrusted() ) {
        qCWarning(lcInputSimulator) << "Accessibility permission not granted, cannot simulate mouse event";
        setLastError("需要辅助功能权限");
        return false;
    }
    // 使用 transformCoordinates 进行平台相关坐标变换（例如 macOS 需要翻转 Y）
    QPoint transformed = transformCoordinates(QPoint(x, y));
    int tx = transformed.x();
    int ty = transformed.y();

    // 创建事件源以提高事件注入的可靠性
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if ( !source ) {
        qCWarning(lcInputSimulator) << "Failed to create CGEventSource";
    }

    CGPoint point = CGPointMake(static_cast<CGFloat>(tx), static_cast<CGFloat>(ty));
    CGEventRef event = CGEventCreateMouseEvent(source, eventType, point, button);

    if ( event ) {
        // 发布到 HID tap，这通常对远程输入更可靠
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        if ( source ) CFRelease(source);
        qCDebug(lcInputSimulator) << "Mouse event simulated:" << eventType << "orig:" << x << y << "transformed:" << tx << ty << "button:" << button;
        return true;
    }

    if ( source ) CFRelease(source);
    qCWarning(lcInputSimulator) << "Failed to create CGEvent for mouse at" << x << y << "(transformed:" << tx << ty << ")";
    return false;
}

bool InputSimulator::simulateKeyboardMacOS(CGKeyCode key, bool keyDown, CGEventFlags modifiers) {
    // 检查辅助功能权限
    if ( !AXIsProcessTrusted() ) {
        qCWarning(lcInputSimulator) << "Accessibility permission not granted, cannot simulate keyboard event";
        setLastError("需要辅助功能权限");
        return false;
    }

    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, key, keyDown);

    if ( event ) {
        // 设置修饰键标志（如果有）
        if ( modifiers != 0 ) {
            CGEventSetFlags(event, modifiers);
        }
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        qCDebug(lcInputSimulator) << "Keyboard event simulated: key=" << key
            << "keyDown=" << keyDown << "modifiers=" << modifiers;
        return true;
    }

    qCWarning(lcInputSimulator) << "Failed to create CGEvent for keyboard key:" << key;
    return false;
}

CGKeyCode InputSimulator::qtKeyToMacOSKey(int qtKey) {
    // macOS 虚拟键码映射表
    // 参考: https://developer.apple.com/documentation/coregraphics/cgkeycode
    switch ( qtKey ) {
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
            qCWarning(lcInputSimulator) << "Unmapped Qt key:" << Qt::hex << qtKey << Qt::dec
                << "(" << qtKey << ") - trying direct conversion";
            return static_cast<CGKeyCode>(qtKey & 0xFF);
    }
}

CGEventFlags InputSimulator::qtModifiersToMacOSModifiers(Qt::KeyboardModifiers modifiers) {
    CGEventFlags result = 0;
    if ( modifiers & Qt::ControlModifier ) result |= kCGEventFlagMaskControl;
    if ( modifiers & Qt::ShiftModifier ) result |= kCGEventFlagMaskShift;
    if ( modifiers & Qt::AltModifier ) result |= kCGEventFlagMaskAlternate;
    if ( modifiers & Qt::MetaModifier ) result |= kCGEventFlagMaskCommand;
    return result;
}
#endif

#ifdef Q_OS_LINUX
bool InputSimulator::initializeLinux() {
    m_display = XOpenDisplay(nullptr);
    return m_display != nullptr;
}

void InputSimulator::cleanupLinux() {
    if ( m_display ) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
}

bool InputSimulator::simulateMouseLinux(int x, int y, unsigned int button, bool press) {
    if ( !m_display ) return false;

    if ( button == 0 ) {
        // 鼠标移动
        return XTestFakeMotionEvent(m_display, -1, x, y, CurrentTime) == True;
    } else {
        // 鼠标按键
        return XTestFakeButtonEvent(m_display, button, press ? True : False, CurrentTime) == True;
    }
}

bool InputSimulator::simulateKeyboardLinux(KeySym key, bool press, unsigned int modifiers) {
    if ( !m_display ) return false;

    KeyCode keycode = XKeysymToKeycode(m_display, key);
    if ( keycode == 0 ) return false;

    // 按下修饰键
    if ( press ) {
        if ( modifiers & ControlMask ) {
            KeyCode ctrlKey = XKeysymToKeycode(m_display, XK_Control_L);
            XTestFakeKeyEvent(m_display, ctrlKey, True, CurrentTime);
        }
        if ( modifiers & ShiftMask ) {
            KeyCode shiftKey = XKeysymToKeycode(m_display, XK_Shift_L);
            XTestFakeKeyEvent(m_display, shiftKey, True, CurrentTime);
        }
        if ( modifiers & Mod1Mask ) {  // Alt
            KeyCode altKey = XKeysymToKeycode(m_display, XK_Alt_L);
            XTestFakeKeyEvent(m_display, altKey, True, CurrentTime);
        }
    }

    // 主键事件
    bool result = XTestFakeKeyEvent(m_display, keycode, press ? True : False, CurrentTime) == True;

    // 释放修饰键
    if ( !press ) {
        if ( modifiers & Mod1Mask ) {  // Alt
            KeyCode altKey = XKeysymToKeycode(m_display, XK_Alt_L);
            XTestFakeKeyEvent(m_display, altKey, False, CurrentTime);
        }
        if ( modifiers & ShiftMask ) {
            KeyCode shiftKey = XKeysymToKeycode(m_display, XK_Shift_L);
            XTestFakeKeyEvent(m_display, shiftKey, False, CurrentTime);
        }
        if ( modifiers & ControlMask ) {
            KeyCode ctrlKey = XKeysymToKeycode(m_display, XK_Control_L);
            XTestFakeKeyEvent(m_display, ctrlKey, False, CurrentTime);
        }
    }

    XFlush(m_display);
    return result;
}

KeySym InputSimulator::qtKeyToLinuxKey(int qtKey) {
    // Qt Key 到 X11 KeySym 的映射
    // 参考: /usr/include/X11/keysymdef.h
    switch ( qtKey ) {
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
            qCDebug(lcInputSimulator) << "Unmapped Qt key:" << qtKey << "using default mapping";
            return static_cast<KeySym>(qtKey);
    }
}

unsigned int InputSimulator::qtModifiersToLinuxModifiers(Qt::KeyboardModifiers modifiers) {
    unsigned int result = 0;
    if ( modifiers & Qt::ControlModifier ) result |= ControlMask;
    if ( modifiers & Qt::ShiftModifier ) result |= ShiftMask;
    if ( modifiers & Qt::AltModifier ) result |= Mod1Mask;
    return result;
}
#endif