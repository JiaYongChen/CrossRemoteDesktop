#ifndef INPUTSIMULATOR_H
#define INPUTSIMULATOR_H

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QMutex>
#include <memory>

// 前向声明
class MouseSimulator;
class KeyboardSimulator;

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

private:
    // 组合的模拟器对象
    std::unique_ptr<MouseSimulator> m_mouseSimulator;
    std::unique_ptr<KeyboardSimulator> m_keyboardSimulator;

    // 辅助方法
    void setLastError(const QString& error);

    // 状态
    bool m_initialized;
    QString m_lastError;

    // 线程安全
    QMutex m_mutex;
};

#endif // INPUTSIMULATOR_H