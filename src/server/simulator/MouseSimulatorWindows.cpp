#include "MouseSimulatorWindows.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMouseSimulatorWindows, "simulator.mouse.windows")

MouseSimulatorWindows::MouseSimulatorWindows() : MouseSimulator() {
}

MouseSimulatorWindows::~MouseSimulatorWindows() {
    cleanup();
}

bool MouseSimulatorWindows::initialize() {
    if (m_initialized) {
        return true;
    }

    // Windows API 不需要特殊初始化
    m_screenSize = getScreenSize();
    m_initialized = true;
    qDebug() << "MouseSimulatorWindows: Initialized successfully";
    return true;
}

void MouseSimulatorWindows::cleanup() {
    // Windows API 不需要特殊清理
    m_initialized = false;
}

bool MouseSimulatorWindows::simulateMouseMove(int x, int y) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    return simulateMouseEvent(x, y, MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE);
}

bool MouseSimulatorWindows::simulateMousePress(int x, int y, Qt::MouseButton button) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    DWORD flags = qtButtonToWindowsFlags(button, true);
    if (flags == 0) {
        return false;
    }

    return simulateMouseEvent(x, y, flags | MOUSEEVENTF_ABSOLUTE);
}

bool MouseSimulatorWindows::simulateMouseRelease(int x, int y, Qt::MouseButton button) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    DWORD flags = qtButtonToWindowsFlags(button, false);
    if (flags == 0) {
        return false;
    }

    return simulateMouseEvent(x, y, flags | MOUSEEVENTF_ABSOLUTE);
}

bool MouseSimulatorWindows::simulateMouseWheel(int x, int y, int deltaX, int deltaY) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    bool result = true;
    
    // 处理垂直滚动
    if (deltaY != 0) {
        result = result && simulateMouseEvent(x, y, MOUSEEVENTF_WHEEL, deltaY);
    }
    
    // 处理水平滚动
    if (deltaX != 0) {
        result = result && simulateMouseEvent(x, y, MOUSEEVENTF_HWHEEL, deltaX);
    }

    return result;
}

QSize MouseSimulatorWindows::getScreenSize() const {
    return QSize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

QPoint MouseSimulatorWindows::getCursorPosition() const {
    POINT point;
    if (GetCursorPos(&point)) {
        return QPoint(point.x, point.y);
    }
    return QPoint();
}

int MouseSimulatorWindows::getCurrentCursorType() const {
    // Windows 实现：获取当前光标类型
    // 使用 GetCursorInfo 获取光标句柄，然后判断类型
    CURSORINFO cursorInfo;
    cursorInfo.cbSize = sizeof(CURSORINFO);
    
    if (GetCursorInfo(&cursorInfo)) {
        HCURSOR hCursor = cursorInfo.hCursor;
        
        // 通过比较系统标准光标句柄来判断类型
        if (hCursor == LoadCursor(NULL, IDC_ARROW)) return 0; // ARROW
        if (hCursor == LoadCursor(NULL, IDC_IBEAM)) return 1; // IBEAM
        if (hCursor == LoadCursor(NULL, IDC_WAIT)) return 2;  // WAIT
        if (hCursor == LoadCursor(NULL, IDC_CROSS)) return 3; // CROSS
        if (hCursor == LoadCursor(NULL, IDC_HAND)) return 4;  // HAND
        if (hCursor == LoadCursor(NULL, IDC_SIZEALL)) return 5; // SIZE_ALL
        if (hCursor == LoadCursor(NULL, IDC_SIZENESW)) return 6; // SIZE_NESW
        if (hCursor == LoadCursor(NULL, IDC_SIZENS)) return 7; // SIZE_NS
        if (hCursor == LoadCursor(NULL, IDC_SIZENWSE)) return 8; // SIZE_NWSE
        if (hCursor == LoadCursor(NULL, IDC_SIZEWE)) return 9; // SIZE_WE
        if (hCursor == LoadCursor(NULL, IDC_NO)) return 10;   // NO
        if (hCursor == LoadCursor(NULL, IDC_HELP)) return 11; // HELP
        if (hCursor == LoadCursor(NULL, IDC_APPSTARTING)) return 12; // BUSY
    }
    
    return 0; // 默认返回 ARROW
}

bool MouseSimulatorWindows::simulateMouseEvent(int x, int y, DWORD flags, DWORD data) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    
    // 将屏幕坐标转换为绝对坐标 (0-65535)
    input.mi.dx = (x * 65535) / GetSystemMetrics(SM_CXSCREEN);
    input.mi.dy = (y * 65535) / GetSystemMetrics(SM_CYSCREEN);
    input.mi.dwFlags = flags;
    input.mi.mouseData = data;

    UINT sent = SendInput(1, &input, sizeof(INPUT));
    if (sent == 1) {
        qCDebug(lcMouseSimulatorWindows) << "Mouse event simulated: x=" << x << "y=" << y 
            << "flags=" << flags << "data=" << data;
        return true;
    }

    qCWarning(lcMouseSimulatorWindows) << "Failed to send mouse input: x=" << x << "y=" << y;
    return false;
}

DWORD MouseSimulatorWindows::qtButtonToWindowsFlags(Qt::MouseButton button, bool isPress) const {
    switch (button) {
        case Qt::LeftButton:
            return isPress ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        case Qt::RightButton:
            return isPress ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        case Qt::MiddleButton:
            return isPress ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        default:
            return 0;
    }
}

#endif // Q_OS_WIN
