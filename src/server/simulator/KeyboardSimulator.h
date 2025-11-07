#ifndef KEYBOARDSIMULATOR_H
#define KEYBOARDSIMULATOR_H

#include <QtCore/QObject>
#include <QtCore/QString>

/**
 * @brief 键盘模拟器抽象基类
 * 
 * 定义跨平台的键盘模拟接口
 */
class KeyboardSimulator : public QObject {
    Q_OBJECT

public:
    explicit KeyboardSimulator(QObject* parent = nullptr);
    virtual ~KeyboardSimulator();

    // 初始化和清理
    virtual bool initialize() = 0;
    virtual void cleanup() = 0;
    bool isInitialized() const { return m_initialized; }

    // 键盘操作
    virtual bool simulateKeyPress(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) = 0;
    virtual bool simulateKeyRelease(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) = 0;

    // 配置
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // 错误处理
    QString lastError() const { return m_lastError; }

protected:
    void setLastError(const QString& error);
    bool isValidKey(int key) const;

    bool m_initialized;
    bool m_enabled;
    QString m_lastError;
};

#endif // KEYBOARDSIMULATOR_H
