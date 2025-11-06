#ifndef INPUTSIMULATOR_H
#define INPUTSIMULATOR_H

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QMutex>

// 平台特定包含
#ifdef Q_OS_WIN
#include <Windows.h>
#endif

#ifdef Q_OS_MACOS
#include <ApplicationServices/ApplicationServices.h>
#endif

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <X11/extensions/Xtest.h>
#endif

class InputSimulator : public QObject {
    Q_OBJECT

public:
    explicit InputSimulator(QObject* parent = nullptr);
    ~InputSimulator();

    // 初始化和清理
    bool initialize();
    void cleanup();
    bool isInitialized() const;

    // 鼠标模拟
    bool simulateMouseMove(int x, int y);
    bool simulateMousePress(int x, int y, Qt::MouseButton button);
    bool simulateMouseRelease(int x, int y, Qt::MouseButton button);
    bool simulateMouseDoubleClick(int x, int y, Qt::MouseButton button);
    bool simulateMouseWheel(int x, int y, int delta);

    // 键盘模拟
    bool simulateKeyPress(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    bool simulateKeyRelease(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    // 屏幕信息
    QSize getScreenSize() const;
    QPoint getCursorPosition() const;

    // 配置
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // 错误处理
    QString lastError() const;

    // macOS 辅助功能权限检查
#ifdef Q_OS_MACOS
    static bool checkAccessibilityPermission();
    static bool requestAccessibilityPermission();
#endif

private:
    // 平台特定实现
#ifdef Q_OS_WIN
    bool initializeWindows();
    void cleanupWindows();
    bool simulateMouseWindows(int x, int y, DWORD flags, DWORD data = 0);
    bool simulateKeyboardWindows(WORD key, DWORD flags, DWORD modifiers);
    WORD qtKeyToWindowsKey(int qtKey);
    DWORD qtModifiersToWindowsModifiers(Qt::KeyboardModifiers modifiers);
#endif

#ifdef Q_OS_MACOS
    bool initializeMacOS();
    void cleanupMacOS();
    bool simulateMouseMacOS(int x, int y, CGEventType eventType, CGMouseButton button = kCGMouseButtonLeft);
    bool simulateKeyboardMacOS(CGKeyCode key, bool keyDown, CGEventFlags modifiers);
    CGKeyCode qtKeyToMacOSKey(int qtKey);
    CGEventFlags qtModifiersToMacOSModifiers(Qt::KeyboardModifiers modifiers);
#endif

#ifdef Q_OS_LINUX
    bool initializeLinux();
    void cleanupLinux();
    bool simulateMouseLinux(int x, int y, unsigned int button, bool press);
    bool simulateKeyboardLinux(KeySym key, bool press, unsigned int modifiers);
    KeySym qtKeyToLinuxKey(int qtKey);
    unsigned int qtModifiersToLinuxModifiers(Qt::KeyboardModifiers modifiers);
    Display* m_display;
#endif

    // 辅助方法
    void setLastError(const QString& error);
    void delay(int msecs);
    bool isValidCoordinate(int x, int y) const;
    bool isValidKey(int key) const;
    
    // 内部辅助函数
    bool simulateMouseClick(int x, int y, Qt::MouseButton button);

    // 状态
    bool m_initialized;
    bool m_enabled;
    QString m_lastError;

    // 屏幕信息
    QSize m_screenSize;

    // 线程安全
    QMutex m_mutex;

    // 平台特定数据
#ifdef Q_OS_WIN
    HWND m_targetWindow;
#endif

#ifdef Q_OS_MACOS
    void* m_macosData;
#endif
};

#endif // INPUTSIMULATOR_H