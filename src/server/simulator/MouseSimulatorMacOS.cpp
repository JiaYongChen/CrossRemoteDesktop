#include "MouseSimulatorMacOS.h"

#ifdef Q_OS_MACOS

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMouseSimulatorMacOS, "simulator.mouse.macos")

MouseSimulatorMacOS::MouseSimulatorMacOS() : MouseSimulator() {
}

MouseSimulatorMacOS::~MouseSimulatorMacOS() {
    cleanup();
}

bool MouseSimulatorMacOS::initialize() {
    if (m_initialized) {
        return true;
    }

    // 检查辅助功能权限
    if (!checkAccessibilityPermission()) {
        setLastError("需要辅助功能权限才能模拟输入事件。请在系统偏好设置 > 安全性与隐私 > 隐私 > 辅助功能中授予权限。");
        qWarning() << "MouseSimulatorMacOS: 缺少辅助功能权限";

        // 尝试请求权限（会打开系统设置）
        requestAccessibilityPermission();
        return false;
    }

    m_screenSize = getScreenSize();
    m_initialized = true;
    qDebug() << "MouseSimulatorMacOS: macOS 辅助功能权限已授予";
    return true;
}

void MouseSimulatorMacOS::cleanup() {
    // macOS API 不需要特殊清理
    m_initialized = false;
}

bool MouseSimulatorMacOS::checkAccessibilityPermission() {
    // 检查当前进程是否被信任（有辅助功能权限）
    return AXIsProcessTrusted();
}

bool MouseSimulatorMacOS::requestAccessibilityPermission() {
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

bool MouseSimulatorMacOS::simulateMouseMove(int x, int y) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    return simulateMouseEvent(x, y, kCGEventMouseMoved, kCGMouseButtonLeft);
}

bool MouseSimulatorMacOS::simulateMousePress(int x, int y, Qt::MouseButton button) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    CGEventType eventType;
    CGMouseButton cgButton = qtButtonToMacOSButton(button);
    
    switch (cgButton) {
        case kCGMouseButtonLeft:
            eventType = kCGEventLeftMouseDown;
            break;
        case kCGMouseButtonRight:
            eventType = kCGEventRightMouseDown;
            break;
        case kCGMouseButtonCenter:
            eventType = kCGEventOtherMouseDown;
            break;
        default:
            return false;
    }

    return simulateMouseEvent(x, y, eventType, cgButton);
}

bool MouseSimulatorMacOS::simulateMouseRelease(int x, int y, Qt::MouseButton button) {
    if (!m_initialized || !m_enabled) {
        return false;
    }

    CGEventType eventType;
    CGMouseButton cgButton = qtButtonToMacOSButton(button);
    
    switch (cgButton) {
        case kCGMouseButtonLeft:
            eventType = kCGEventLeftMouseUp;
            break;
        case kCGMouseButtonRight:
            eventType = kCGEventRightMouseUp;
            break;
        case kCGMouseButtonCenter:
            eventType = kCGEventOtherMouseUp;
            break;
        default:
            return false;
    }

    return simulateMouseEvent(x, y, eventType, cgButton);
}

bool MouseSimulatorMacOS::simulateMouseDoubleClick(int x, int y, Qt::MouseButton button) {
    if (!m_initialized || !m_enabled) {
        setLastError("MouseSimulator not initialized or disabled");
        return false;
    }

    // 检查辅助功能权限
    if (!AXIsProcessTrusted()) {
        qCWarning(lcMouseSimulatorMacOS) << "Accessibility permission not granted";
        setLastError("需要辅助功能权限");
        return false;
    }

    CGMouseButton cgButton = qtButtonToMacOSButton(button);
    CGEventType downType, upType;
    
    switch (cgButton) {
        case kCGMouseButtonLeft:
            downType = kCGEventLeftMouseDown;
            upType = kCGEventLeftMouseUp;
            break;
        case kCGMouseButtonRight:
            downType = kCGEventRightMouseDown;
            upType = kCGEventRightMouseUp;
            break;
        case kCGMouseButtonCenter:
            downType = kCGEventOtherMouseDown;
            upType = kCGEventOtherMouseUp;
            break;
        default:
            return false;
    }

    // macOS 坐标转换
    QSize screenSize = getScreenSize();
    int tx = x;
    int ty = screenSize.height() - y - 1;
    CGPoint point = CGPointMake(static_cast<CGFloat>(tx), static_cast<CGFloat>(ty));

    // 创建事件源
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!source) {
        qCWarning(lcMouseSimulatorMacOS) << "Failed to create CGEventSource";
        return false;
    }

    // 第一次点击（clickCount = 1）
    CGEventRef downEvent1 = CGEventCreateMouseEvent(source, downType, point, cgButton);
    CGEventRef upEvent1 = CGEventCreateMouseEvent(source, upType, point, cgButton);
    if (downEvent1 && upEvent1) {
        CGEventSetIntegerValueField(downEvent1, kCGMouseEventClickState, 1);
        CGEventSetIntegerValueField(upEvent1, kCGMouseEventClickState, 1);
        CGEventPost(kCGHIDEventTap, downEvent1);
        CGEventPost(kCGHIDEventTap, upEvent1);
        CFRelease(downEvent1);
        CFRelease(upEvent1);
    } else {
        if (source) CFRelease(source);
        return false;
    }

    // 第二次点击（clickCount = 2）- 这标识为双击
    CGEventRef downEvent2 = CGEventCreateMouseEvent(source, downType, point, cgButton);
    CGEventRef upEvent2 = CGEventCreateMouseEvent(source, upType, point, cgButton);
    if (downEvent2 && upEvent2) {
        CGEventSetIntegerValueField(downEvent2, kCGMouseEventClickState, 2);
        CGEventSetIntegerValueField(upEvent2, kCGMouseEventClickState, 2);
        CGEventPost(kCGHIDEventTap, downEvent2);
        CGEventPost(kCGHIDEventTap, upEvent2);
        CFRelease(downEvent2);
        CFRelease(upEvent2);
    } else {
        if (source) CFRelease(source);
        return false;
    }

    CFRelease(source);
    qCDebug(lcMouseSimulatorMacOS) << "Double click simulated at" << x << y << "button:" << button;
    return true;
}

bool MouseSimulatorMacOS::simulateMouseWheel(int x, int y, int deltaX, int deltaY) {
    Q_UNUSED(x);  // macOS 滚轮事件不需要坐标,在当前鼠标位置生效
    Q_UNUSED(y);
    
    if (!m_initialized || !m_enabled) {
        return false;
    }

    // 检查辅助功能权限
    if (!AXIsProcessTrusted()) {
        qCWarning(lcMouseSimulatorMacOS) << "Accessibility permission not granted";
        setLastError("需要辅助功能权限");
        return false;
    }

    // macOS 滚轮事件,deltaY 为正值向上滚动,负值向下滚动
    CGEventRef event = CGEventCreateScrollWheelEvent(
        nullptr,
        kCGScrollEventUnitPixel,
        2,  // 轴数量（垂直和水平）
        deltaY,
        deltaX
    );

    if (event) {
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        qCDebug(lcMouseSimulatorMacOS) << "Mouse wheel simulated: deltaX=" << deltaX << "deltaY=" << deltaY;
        return true;
    }

    qCWarning(lcMouseSimulatorMacOS) << "Failed to create scroll wheel event";
    return false;
}

QSize MouseSimulatorMacOS::getScreenSize() const {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        return screen->size();
    }
    return QSize(0, 0);
}

QPoint MouseSimulatorMacOS::getCursorPosition() const {
    CGEventRef event = CGEventCreate(nullptr);
    CGPoint point = CGEventGetLocation(event);
    if (event) {
        CFRelease(event);
    }
    
    // macOS 坐标系原点在左下角，需要转换为左上角坐标系
    QSize screenSize = getScreenSize();
    return QPoint(static_cast<int>(point.x), screenSize.height() - static_cast<int>(point.y) - 1);
}

int MouseSimulatorMacOS::getCurrentCursorType() const {
    // TODO: 实现 macOS 平台的光标类型检测并映射到 Qt::CursorShape
    // 使用 NSCursor API 获取当前光标，然后映射到对应的 Qt::CursorShape 枚举值
    return Qt::ArrowCursor; // 默认返回 Qt::ArrowCursor (0)
}

bool MouseSimulatorMacOS::simulateMouseEvent(int x, int y, CGEventType eventType, CGMouseButton button) {
    // 检查辅助功能权限
    if (!AXIsProcessTrusted()) {
        qCWarning(lcMouseSimulatorMacOS) << "Accessibility permission not granted, cannot simulate mouse event";
        setLastError("需要辅助功能权限");
        return false;
    }

    // macOS 需要翻转 Y 坐标(屏幕坐标系原点在左上角，但 CoreGraphics 原点在左下角)
    QSize screenSize = getScreenSize();
    int tx = x;
    int ty = screenSize.height() - y - 1;

    // 创建事件源以提高事件注入的可靠性
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!source) {
        qCWarning(lcMouseSimulatorMacOS) << "Failed to create CGEventSource";
    }

    CGPoint point = CGPointMake(static_cast<CGFloat>(tx), static_cast<CGFloat>(ty));
    CGEventRef event = CGEventCreateMouseEvent(source, eventType, point, button);

    if (event) {
        // 发布到 HID tap，这通常对远程输入更可靠
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        if (source) CFRelease(source);
        qCDebug(lcMouseSimulatorMacOS) << "Mouse event simulated:" << eventType 
            << "orig:" << x << y << "transformed:" << tx << ty << "button:" << button;
        return true;
    }

    if (source) CFRelease(source);
    qCWarning(lcMouseSimulatorMacOS) << "Failed to create CGEvent for mouse at" << x << y 
        << "(transformed:" << tx << ty << ")";
    return false;
}

CGMouseButton MouseSimulatorMacOS::qtButtonToMacOSButton(Qt::MouseButton button) const {
    switch (button) {
        case Qt::LeftButton:
            return kCGMouseButtonLeft;
        case Qt::RightButton:
            return kCGMouseButtonRight;
        case Qt::MiddleButton:
            return kCGMouseButtonCenter;
        default:
            return kCGMouseButtonLeft;
    }
}

#endif // Q_OS_MACOS
