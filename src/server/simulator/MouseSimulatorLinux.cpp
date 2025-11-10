#include "MouseSimulatorLinux.h"

#ifdef Q_OS_LINUX

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMouseSimulatorLinux, "simulator.mouse.linux")

MouseSimulatorLinux::MouseSimulatorLinux() : MouseSimulator(), m_display(nullptr) {
}

MouseSimulatorLinux::~MouseSimulatorLinux() {
    cleanup();
}

bool MouseSimulatorLinux::initialize() {
    if (m_initialized) {
        return true;
    }

    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        setLastError("Failed to open X11 display");
        qWarning() << "MouseSimulatorLinux: Failed to open X11 display";
        return false;
    }

    m_screenSize = getScreenSize();
    m_initialized = true;
    qDebug() << "MouseSimulatorLinux: Initialized successfully";
    return true;
}

void MouseSimulatorLinux::cleanup() {
    if (m_display) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
    m_initialized = false;
}

bool MouseSimulatorLinux::simulateMouseMove(int x, int y) {
    if (!m_initialized || !m_enabled || !m_display) {
        return false;
    }

    // button = 0 表示鼠标移动
    return simulateMouseEvent(x, y, 0, true);
}

bool MouseSimulatorLinux::simulateMousePress(int x, int y, Qt::MouseButton button) {
    if (!m_initialized || !m_enabled || !m_display) {
        return false;
    }

    unsigned int x11Button = qtButtonToX11Button(button);
    if (x11Button == 0) {
        return false;
    }

    return simulateMouseEvent(x, y, x11Button, true);
}

bool MouseSimulatorLinux::simulateMouseRelease(int x, int y, Qt::MouseButton button) {
    if (!m_initialized || !m_enabled || !m_display) {
        return false;
    }

    unsigned int x11Button = qtButtonToX11Button(button);
    if (x11Button == 0) {
        return false;
    }

    return simulateMouseEvent(x, y, x11Button, false);
}

bool MouseSimulatorLinux::simulateMouseWheel(int x, int y, int deltaX, int deltaY) {
    if (!m_initialized || !m_enabled || !m_display) {
        return false;
    }

    bool result = true;
    
    // X11 滚轮事件：button 4 = 向上, button 5 = 向下
    // button 6 = 向左, button 7 = 向右
    if (deltaY > 0) {
        // 向上滚动
        result = result && simulateMouseEvent(x, y, 4, true);
        result = result && simulateMouseEvent(x, y, 4, false);
    } else if (deltaY < 0) {
        // 向下滚动
        result = result && simulateMouseEvent(x, y, 5, true);
        result = result && simulateMouseEvent(x, y, 5, false);
    }

    if (deltaX > 0) {
        // 向右滚动
        result = result && simulateMouseEvent(x, y, 7, true);
        result = result && simulateMouseEvent(x, y, 7, false);
    } else if (deltaX < 0) {
        // 向左滚动
        result = result && simulateMouseEvent(x, y, 6, true);
        result = result && simulateMouseEvent(x, y, 6, false);
    }

    return result;
}

QSize MouseSimulatorLinux::getScreenSize() const {
    if (m_display) {
        Screen* screen = DefaultScreenOfDisplay(m_display);
        return QSize(WidthOfScreen(screen), HeightOfScreen(screen));
    }
    return QSize(0, 0);
}

QPoint MouseSimulatorLinux::getCursorPosition() const {
    if (m_display) {
        Window root, child;
        int rootX, rootY, winX, winY;
        unsigned int mask;
        if (XQueryPointer(m_display, DefaultRootWindow(m_display), &root, &child, 
                          &rootX, &rootY, &winX, &winY, &mask)) {
            return QPoint(rootX, rootY);
        }
    }
    return QPoint();
}

int MouseSimulatorLinux::getCurrentCursorType() const {
    // Linux X11 实现：获取当前光标类型并映射到 Qt::CursorShape
    // 这需要 XFixes 扩展来获取光标信息，然后映射到对应的 Qt::CursorShape 枚举值
    // 暂时返回默认值，完整实现需要额外的 X11 扩展
    return Qt::ArrowCursor; // 默认返回 Qt::ArrowCursor (0)
}

bool MouseSimulatorLinux::simulateMouseEvent(int x, int y, unsigned int button, bool press) {
    if (!m_display) {
        return false;
    }

    bool result = false;
    
    if (button == 0) {
        // 鼠标移动
        result = XTestFakeMotionEvent(m_display, -1, x, y, CurrentTime) == True;
        XFlush(m_display);
        qCDebug(lcMouseSimulatorLinux) << "Mouse move: x=" << x << "y=" << y << "result=" << result;
    } else {
        // 鼠标按键
        result = XTestFakeButtonEvent(m_display, button, press ? True : False, CurrentTime) == True;
        XFlush(m_display);
        qCDebug(lcMouseSimulatorLinux) << "Mouse button: button=" << button 
            << "press=" << press << "result=" << result;
    }

    return result;
}

unsigned int MouseSimulatorLinux::qtButtonToX11Button(Qt::MouseButton button) const {
    switch (button) {
        case Qt::LeftButton:
            return 1;
        case Qt::MiddleButton:
            return 2;
        case Qt::RightButton:
            return 3;
        default:
            return 0;
    }
}

#endif // Q_OS_LINUX
