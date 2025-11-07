#ifndef MOUSESIMULATOR_H
#define MOUSESIMULATOR_H

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QSize>
#include <QtCore/QString>

/**
 * @brief 鼠标模拟器抽象基类
 * 
 * 定义跨平台的鼠标模拟接口
 */
class MouseSimulator : public QObject {
    Q_OBJECT

public:
    explicit MouseSimulator(QObject* parent = nullptr);
    virtual ~MouseSimulator();

    // 初始化和清理
    virtual bool initialize() = 0;
    virtual void cleanup() = 0;
    bool isInitialized() const { return m_initialized; }

    // 鼠标操作
    virtual bool simulateMouseMove(int x, int y) = 0;
    virtual bool simulateMousePress(int x, int y, Qt::MouseButton button) = 0;
    virtual bool simulateMouseRelease(int x, int y, Qt::MouseButton button) = 0;
    virtual bool simulateMouseDoubleClick(int x, int y, Qt::MouseButton button);
    virtual bool simulateMouseWheel(int x, int y, int deltaX, int deltaY) = 0;

    // 屏幕信息
    virtual QSize getScreenSize() const = 0;
    virtual QPoint getCursorPosition() const = 0;

    // 配置
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // 错误处理
    QString lastError() const { return m_lastError; }

protected:
    void setLastError(const QString& error);
    bool isValidCoordinate(int x, int y) const;
    bool simulateMouseClick(int x, int y, Qt::MouseButton button);

    bool m_initialized;
    bool m_enabled;
    QString m_lastError;
    QSize m_screenSize;
};

#endif // MOUSESIMULATOR_H
