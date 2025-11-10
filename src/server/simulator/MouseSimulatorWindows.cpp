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
    // Windows 实现：获取当前光标类型并映射到 Qt::CursorShape
    // 使用 GetCursorInfo 获取光标句柄，然后判断类型
    CURSORINFO cursorInfo;
    cursorInfo.cbSize = sizeof(CURSORINFO);
    
    if (GetCursorInfo(&cursorInfo)) {
        HCURSOR hCursor = cursorInfo.hCursor;
        
        // 将 Windows 光标映射到 Qt::CursorShape 枚举值
        if (hCursor == LoadCursor(NULL, IDC_ARROW)) return Qt::ArrowCursor;           // 0
        if (hCursor == LoadCursor(NULL, IDC_IBEAM)) return Qt::IBeamCursor;           // 4
        if (hCursor == LoadCursor(NULL, IDC_WAIT)) return Qt::WaitCursor;             // 3
        if (hCursor == LoadCursor(NULL, IDC_CROSS)) return Qt::CrossCursor;           // 2
        if (hCursor == LoadCursor(NULL, IDC_HAND)) return Qt::PointingHandCursor;     // 13
        if (hCursor == LoadCursor(NULL, IDC_SIZEALL)) return Qt::SizeAllCursor;       // 9
        if (hCursor == LoadCursor(NULL, IDC_SIZENESW)) return Qt::SizeBDiagCursor;    // 7
        if (hCursor == LoadCursor(NULL, IDC_SIZENS)) return Qt::SizeVerCursor;        // 5
        if (hCursor == LoadCursor(NULL, IDC_SIZENWSE)) return Qt::SizeFDiagCursor;    // 6
        if (hCursor == LoadCursor(NULL, IDC_SIZEWE)) return Qt::SizeHorCursor;        // 8
        if (hCursor == LoadCursor(NULL, IDC_NO)) return Qt::ForbiddenCursor;          // 14
        if (hCursor == LoadCursor(NULL, IDC_HELP)) return Qt::WhatsThisCursor;        // 15
        if (hCursor == LoadCursor(NULL, IDC_APPSTARTING)) return Qt::BusyCursor;      // 16
    }
    
    return Qt::ArrowCursor; // 默认返回 Qt::ArrowCursor (0)
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
