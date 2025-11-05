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

InputSimulator::InputSimulator(QObject *parent)
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

InputSimulator::~InputSimulator()
{
    cleanup();
}

bool InputSimulator::initialize()
{
    if (m_initialized) {
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
    
    if (m_initialized) {
        m_screenSize = getScreenSize();
    }
    
    return m_initialized;
}

void InputSimulator::cleanup()
{
    if (!m_initialized) {
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

bool InputSimulator::isInitialized() const
{
    return m_initialized;
}

bool InputSimulator::simulateMouseMove(int x, int y)
{
    if (!m_initialized || !m_enabled) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }
    
    if (!isValidCoordinate(x, y)) {
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
    
    if (result) {
        emit mouseSimulated(x, y, Qt::NoButton, "move");
    } else {
        qCWarning(lcInputSimulator) << "Failed to simulate mouse move to" << x << y;
    }
    
    return result;
}

bool InputSimulator::simulateMousePress(int x, int y, Qt::MouseButton button)
{
    if (!m_initialized || !m_enabled) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }
    
    if (!isValidCoordinate(x, y)) {
        setLastError("Invalid coordinates");
        return false;
    }
    
    bool result = false;
    
#ifdef Q_OS_WIN
    DWORD flags = MOUSEEVENTF_ABSOLUTE;
    if (button == Qt::LeftButton) flags |= MOUSEEVENTF_LEFTDOWN;
    else if (button == Qt::RightButton) flags |= MOUSEEVENTF_RIGHTDOWN;
    else if (button == Qt::MiddleButton) flags |= MOUSEEVENTF_MIDDLEDOWN;
    result = simulateMouseWindows(x, y, flags);
#elif defined(Q_OS_MACOS)
    CGEventType eventType = kCGEventLeftMouseDown;
    CGMouseButton cgButton = kCGMouseButtonLeft;
    if (button == Qt::RightButton) {
        eventType = kCGEventRightMouseDown;
        cgButton = kCGMouseButtonRight;
    } else if (button == Qt::MiddleButton) {
        eventType = kCGEventOtherMouseDown;
        cgButton = kCGMouseButtonCenter;
    }
    result = simulateMouseMacOS(x, y, eventType, cgButton);
#elif defined(Q_OS_LINUX)
    unsigned int linuxButton = 1;
    if (button == Qt::RightButton) linuxButton = 3;
    else if (button == Qt::MiddleButton) linuxButton = 2;
    result = simulateMouseLinux(x, y, linuxButton, true);
#endif
    
    if (result) {
        emit mouseSimulated(x, y, button, "press");
    } else {
        qCWarning(lcInputSimulator) << "Failed to simulate mouse press at" << x << y << "button:" << button;
    }
    
    return result;
}

bool InputSimulator::simulateMouseRelease(int x, int y, Qt::MouseButton button)
{
    if (!m_initialized || !m_enabled) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }
    
    if (!isValidCoordinate(x, y)) {
        setLastError("Invalid coordinates");
        return false;
    }
    
    bool result = false;
    
#ifdef Q_OS_WIN
    DWORD flags = MOUSEEVENTF_ABSOLUTE;
    if (button == Qt::LeftButton) flags |= MOUSEEVENTF_LEFTUP;
    else if (button == Qt::RightButton) flags |= MOUSEEVENTF_RIGHTUP;
    else if (button == Qt::MiddleButton) flags |= MOUSEEVENTF_MIDDLEUP;
    result = simulateMouseWindows(x, y, flags);
#elif defined(Q_OS_MACOS)
    CGEventType eventType = kCGEventLeftMouseUp;
    CGMouseButton cgButton = kCGMouseButtonLeft;
    if (button == Qt::RightButton) {
        eventType = kCGEventRightMouseUp;
        cgButton = kCGMouseButtonRight;
    } else if (button == Qt::MiddleButton) {
        eventType = kCGEventOtherMouseUp;
        cgButton = kCGMouseButtonCenter;
    }
    result = simulateMouseMacOS(x, y, eventType, cgButton);
#elif defined(Q_OS_LINUX)
    unsigned int linuxButton = 1;
    if (button == Qt::RightButton) linuxButton = 3;
    else if (button == Qt::MiddleButton) linuxButton = 2;
    result = simulateMouseLinux(x, y, linuxButton, false);
#endif
    
    if (result) {
        emit mouseSimulated(x, y, button, "release");
    } else {
        qCWarning(lcInputSimulator) << "Failed to simulate mouse release at" << x << y << "button:" << button;
    }
    
    return result;
}

bool InputSimulator::simulateMouseClick(int x, int y, Qt::MouseButton button)
{
    bool pressResult = simulateMousePress(x, y, button);
    delay(m_mouseDelay);
    bool releaseResult = simulateMouseRelease(x, y, button);
    
    return pressResult && releaseResult;
}

bool InputSimulator::simulateMouseDoubleClick(int x, int y, Qt::MouseButton button)
{
    bool firstClick = simulateMouseClick(x, y, button);
    delay(50); // 双击间隔
    bool secondClick = simulateMouseClick(x, y, button);
    
    return firstClick && secondClick;
}

bool InputSimulator::simulateMouseWheel(int x, int y, int delta)
{
    if (!m_initialized || !m_enabled) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }
    
    bool result = false;
    
#ifdef Q_OS_WIN
    result = simulateMouseWindows(x, y, MOUSEEVENTF_WHEEL, delta);
#elif defined(Q_OS_MACOS)
    CGEventRef event = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitPixel, 1, delta);
    if (event) {
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        result = true;
    }
#elif defined(Q_OS_LINUX)
    unsigned int button = (delta > 0) ? 4 : 5;
    result = simulateMouseLinux(x, y, button, true) && simulateMouseLinux(x, y, button, false);
#endif
    
    if (result) {
        emit mouseSimulated(x, y, Qt::NoButton, "wheel");
    }
    
    return result;
}

bool InputSimulator::simulateKeyPress(int key, Qt::KeyboardModifiers modifiers)
{
    if (!m_initialized || !m_enabled) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }
    
    if (!isValidKey(key)) {
        setLastError("Invalid key");
        return false;
    }
    
    bool result = false;
    
#ifdef Q_OS_WIN
    WORD winKey = qtKeyToWindowsKey(key);
    DWORD winModifiers = qtModifiersToWindowsModifiers(modifiers);
    result = simulateKeyboardWindows(winKey, 0); // Key down
#elif defined(Q_OS_MACOS)
    CGKeyCode macKey = qtKeyToMacOSKey(key);
    result = simulateKeyboardMacOS(macKey, true);
#elif defined(Q_OS_LINUX)
    KeySym linuxKey = qtKeyToLinuxKey(key);
    result = simulateKeyboardLinux(linuxKey, true);
#endif
    
    if (result) {
        emit keyboardSimulated(key, modifiers, "press");
    }
    
    return result;
}

bool InputSimulator::simulateKeyRelease(int key, Qt::KeyboardModifiers modifiers)
{
    if (!m_initialized || !m_enabled) {
        setLastError("InputSimulator not initialized or disabled");
        return false;
    }
    
    if (!isValidKey(key)) {
        setLastError("Invalid key");
        return false;
    }
    
    bool result = false;
    
#ifdef Q_OS_WIN
    WORD winKey = qtKeyToWindowsKey(key);
    result = simulateKeyboardWindows(winKey, KEYEVENTF_KEYUP);
#elif defined(Q_OS_MACOS)
    CGKeyCode macKey = qtKeyToMacOSKey(key);
    result = simulateKeyboardMacOS(macKey, false);
#elif defined(Q_OS_LINUX)
    KeySym linuxKey = qtKeyToLinuxKey(key);
    result = simulateKeyboardLinux(linuxKey, false);
#endif
    
    if (result) {
        emit keyboardSimulated(key, modifiers, "release");
    }
    
    return result;
}

bool InputSimulator::simulateKeyClick(int key, Qt::KeyboardModifiers modifiers)
{
    bool pressResult = simulateKeyPress(key, modifiers);
    delay(m_keyboardDelay);
    bool releaseResult = simulateKeyRelease(key, modifiers);
    
    return pressResult && releaseResult;
}

bool InputSimulator::simulateTextInput(const QString &text)
{
    for (const QChar &ch : text) {
        int key = ch.unicode();
        if (!simulateKeyClick(key)) {
            return false;
        }
        delay(m_keyboardDelay);
    }
    return true;
}

bool InputSimulator::simulateKeySequence(const QList<int> &keys, Qt::KeyboardModifiers modifiers)
{
    for (int key : keys) {
        if (!simulateKeyClick(key, modifiers)) {
            return false;
        }
        delay(m_keyboardDelay);
    }
    return true;
}

bool InputSimulator::simulateShortcut(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    return simulateKeyClick(static_cast<int>(key), modifiers);
}

QSize InputSimulator::getScreenSize() const
{
#ifdef Q_OS_WIN
    return QSize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
#elif defined(Q_OS_MACOS)
    CGRect screenRect = CGDisplayBounds(CGMainDisplayID());
    return QSize(static_cast<int>(screenRect.size.width), static_cast<int>(screenRect.size.height));
#elif defined(Q_OS_LINUX)
    if (m_display) {
        Screen *screen = DefaultScreenOfDisplay(m_display);
        return QSize(WidthOfScreen(screen), HeightOfScreen(screen));
    }
#endif
    return QSize(1920, 1080); // 默认分辨率
}

QPoint InputSimulator::getCursorPosition() const
{
#ifdef Q_OS_WIN
    POINT point;
    if (GetCursorPos(&point)) {
        return QPoint(point.x, point.y);
    }
#elif defined(Q_OS_MACOS)
    CGEventRef event = CGEventCreate(nullptr);
    if (event) {
        CGPoint point = CGEventGetLocation(event);
        CFRelease(event);
        return QPoint(static_cast<int>(point.x), static_cast<int>(point.y));
    }
#elif defined(Q_OS_LINUX)
    if (m_display) {
        Window root, child;
        int rootX, rootY, winX, winY;
        unsigned int mask;
        if (XQueryPointer(m_display, DefaultRootWindow(m_display), &root, &child, &rootX, &rootY, &winX, &winY, &mask)) {
            return QPoint(rootX, rootY);
        }
    }
#endif
    return QPoint();
}

bool InputSimulator::setCursorPosition(const QPoint &position)
{
    return simulateMouseMove(position.x(), position.y());
}

void InputSimulator::setMouseSpeed(int speed)
{
    m_mouseSpeed = qBound(1, speed, 10);
}

int InputSimulator::mouseSpeed() const
{
    return m_mouseSpeed;
}

void InputSimulator::setKeyboardDelay(int msecs)
{
    m_keyboardDelay = qMax(0, msecs);
}

int InputSimulator::keyboardDelay() const
{
    return m_keyboardDelay;
}

void InputSimulator::setMouseDelay(int msecs)
{
    m_mouseDelay = qMax(0, msecs);
}

int InputSimulator::mouseDelay() const
{
    return m_mouseDelay;
}

void InputSimulator::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool InputSimulator::isEnabled() const
{
    return m_enabled;
}

void InputSimulator::beginBatch()
{
    QMutexLocker locker(&m_mutex);
    m_batchMode = true;
}

void InputSimulator::endBatch()
{
    QMutexLocker locker(&m_mutex);
    m_batchMode = false;
    
    // 处理批量操作
    while (!m_operationQueue.isEmpty()) {
        InputOperation op = m_operationQueue.dequeue();
        // 执行操作
        switch (op.type) {
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

bool InputSimulator::isBatchMode() const
{
    return m_batchMode;
}

QString InputSimulator::lastError() const
{
    return m_lastError;
}

void InputSimulator::simulateInput()
{
    // 处理输入队列
}

void InputSimulator::setLastError(const QString &error)
{
    m_lastError = error;
    if (!error.isEmpty()) {
        emit errorOccurred(error);
    }
}

void InputSimulator::delay(int msecs)
{
    if (msecs > 0) {
        QThread::msleep(msecs);
    }
}

bool InputSimulator::isValidCoordinate(int x, int y) const
{
    return x >= 0 && y >= 0 && x < m_screenSize.width() && y < m_screenSize.height();
}

bool InputSimulator::isValidKey(int key) const
{
    return key > 0 && key <= CoreConstants::Input::MAX_KEY_VALUE;
}

QPoint InputSimulator::transformCoordinates(const QPoint &point) const
{
    // 平台相关的坐标变换
#ifdef Q_OS_MACOS
    // CoreGraphics 的坐标系原点在屏幕左下，Qt 的坐标系原点在左上
    // 需要将 y 翻转为 macOS 全局显示坐标
    int x = point.x();
    int y = point.y();
    int screenH = m_screenSize.height();
    // 防止越界
    if (screenH <= 0) return point;
    int flippedY = screenH - y;
    return QPoint(x, flippedY);
#else
    // 其他平台当前不需要转换
    return point;
#endif
}

#ifdef Q_OS_WIN
bool InputSimulator::initializeWindows()
{
    return true; // Windows API 不需要特殊初始化
}

void InputSimulator::cleanupWindows()
{
    // Windows API 不需要特殊清理
}

bool InputSimulator::simulateMouseWindows(int x, int y, DWORD flags, DWORD data)
{
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dx = (x * 65535) / GetSystemMetrics(SM_CXSCREEN);
    input.mi.dy = (y * 65535) / GetSystemMetrics(SM_CYSCREEN);
    input.mi.dwFlags = flags;
    input.mi.mouseData = data;
    
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool InputSimulator::simulateKeyboardWindows(WORD key, DWORD flags)
{
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwFlags = flags;
    
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

WORD InputSimulator::qtKeyToWindowsKey(int qtKey)
{
    // 简化的键码转换
    switch (qtKey) {
    case Qt::Key_A: return 'A';
    case Qt::Key_B: return 'B';
    case Qt::Key_C: return 'C';
    // ... 更多键码映射
    default: return static_cast<WORD>(qtKey);
    }
}

DWORD InputSimulator::qtModifiersToWindowsModifiers(Qt::KeyboardModifiers modifiers)
{
    DWORD result = 0;
    if (modifiers & Qt::ControlModifier) result |= 0x0002;
    if (modifiers & Qt::ShiftModifier) result |= 0x0004;
    if (modifiers & Qt::AltModifier) result |= 0x0001;
    return result;
}
#endif

#ifdef Q_OS_MACOS
bool InputSimulator::initializeMacOS()
{
    // 检查辅助功能权限
    if (!checkAccessibilityPermission()) {
        setLastError("需要辅助功能权限才能模拟输入事件。请在系统偏好设置 > 安全性与隐私 > 隐私 > 辅助功能中授予权限。");
        qWarning() << "InputSimulator: 缺少辅助功能权限";
        
        // 尝试请求权限（会打开系统设置）
        requestAccessibilityPermission();
        return false;
    }
    
    qDebug() << "InputSimulator: macOS 辅助功能权限已授予";
    return true;
}

void InputSimulator::cleanupMacOS()
{
    // macOS API 不需要特殊清理
}

bool InputSimulator::checkAccessibilityPermission()
{
    // 检查当前进程是否被信任（有辅助功能权限）
    return AXIsProcessTrusted();
}

bool InputSimulator::requestAccessibilityPermission()
{
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
#endif

#ifdef Q_OS_MACOS

bool InputSimulator::simulateMouseMacOS(int x, int y, CGEventType eventType, CGMouseButton button)
{
    // 检查辅助功能权限
    if (!AXIsProcessTrusted()) {
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
    if (!source) {
        qCWarning(lcInputSimulator) << "Failed to create CGEventSource";
    }

    CGPoint point = CGPointMake(static_cast<CGFloat>(tx), static_cast<CGFloat>(ty));
    CGEventRef event = CGEventCreateMouseEvent(source, eventType, point, button);

    if (event) {
        // 发布到 HID tap，这通常对远程输入更可靠
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        if (source) CFRelease(source);
        qCDebug(lcInputSimulator) << "Mouse event simulated:" << eventType << "orig:" << x << y << "transformed:" << tx << ty << "button:" << button;
        return true;
    }

    if (source) CFRelease(source);
    qCWarning(lcInputSimulator) << "Failed to create CGEvent for mouse at" << x << y << "(transformed:" << tx << ty << ")";
    return false;
}

bool InputSimulator::simulateKeyboardMacOS(CGKeyCode key, bool keyDown)
{
    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, key, keyDown);
    
    if (event) {
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        return true;
    }
    
    return false;
}

CGKeyCode InputSimulator::qtKeyToMacOSKey(int qtKey)
{
    // 简化的键码转换
    switch (qtKey) {
    case Qt::Key_A: return 0x00;
    case Qt::Key_B: return 0x0B;
    case Qt::Key_C: return 0x08;
    // ... 更多键码映射
    default: return static_cast<CGKeyCode>(qtKey);
    }
}

CGEventFlags InputSimulator::qtModifiersToMacOSModifiers(Qt::KeyboardModifiers modifiers)
{
    CGEventFlags result = 0;
    if (modifiers & Qt::ControlModifier) result |= kCGEventFlagMaskControl;
    if (modifiers & Qt::ShiftModifier) result |= kCGEventFlagMaskShift;
    if (modifiers & Qt::AltModifier) result |= kCGEventFlagMaskAlternate;
    if (modifiers & Qt::MetaModifier) result |= kCGEventFlagMaskCommand;
    return result;
}
#endif

#ifdef Q_OS_LINUX
bool InputSimulator::initializeLinux()
{
    m_display = XOpenDisplay(nullptr);
    return m_display != nullptr;
}

void InputSimulator::cleanupLinux()
{
    if (m_display) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
}

bool InputSimulator::simulateMouseLinux(int x, int y, unsigned int button, bool press)
{
    if (!m_display) return false;
    
    if (button == 0) {
        // 鼠标移动
        return XTestFakeMotionEvent(m_display, -1, x, y, CurrentTime) == True;
    } else {
        // 鼠标按键
        return XTestFakeButtonEvent(m_display, button, press ? True : False, CurrentTime) == True;
    }
}

bool InputSimulator::simulateKeyboardLinux(KeySym key, bool press)
{
    if (!m_display) return false;
    
    KeyCode keycode = XKeysymToKeycode(m_display, key);
    if (keycode == 0) return false;
    
    return XTestFakeKeyEvent(m_display, keycode, press ? True : False, CurrentTime) == True;
}

KeySym InputSimulator::qtKeyToLinuxKey(int qtKey)
{
    // 简化的键码转换
    switch (qtKey) {
    case Qt::Key_A: return XK_a;
    case Qt::Key_B: return XK_b;
    case Qt::Key_C: return XK_c;
    // ... 更多键码映射
    default: return static_cast<KeySym>(qtKey);
    }
}

unsigned int InputSimulator::qtModifiersToLinuxModifiers(Qt::KeyboardModifiers modifiers)
{
    unsigned int result = 0;
    if (modifiers & Qt::ControlModifier) result |= ControlMask;
    if (modifiers & Qt::ShiftModifier) result |= ShiftMask;
    if (modifiers & Qt::AltModifier) result |= Mod1Mask;
    return result;
}
#endif