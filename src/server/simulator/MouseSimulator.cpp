#include "MouseSimulator.h"
#include <QtCore/QThread>

MouseSimulator::MouseSimulator(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_enabled(true)
    , m_screenSize(1920, 1080) // 默认值
{
}

MouseSimulator::~MouseSimulator() {
}

bool MouseSimulator::simulateMouseDoubleClick(int x, int y, Qt::MouseButton button) {
    if (!m_initialized || !m_enabled) {
        setLastError("MouseSimulator not initialized or disabled");
        return false;
    }

    // 第一次点击：按下 -> 释放
    if (!simulateMousePress(x, y, button)) {
        return false;
    }
    QThread::msleep(10);  // 按下和释放之间的短暂延迟
    if (!simulateMouseRelease(x, y, button)) {
        return false;
    }

    // 双击间隔延迟（约30ms，在系统双击时间范围内）
    QThread::msleep(30);

    // 第二次点击：按下 -> 释放
    if (!simulateMousePress(x, y, button)) {
        return false;
    }
    QThread::msleep(10);  // 按下和释放之间的短暂延迟
    return simulateMouseRelease(x, y, button);
}

bool MouseSimulator::simulateMouseClick(int x, int y, Qt::MouseButton button) {
    if (!simulateMousePress(x, y, button)) {
        return false;
    }
    
    QThread::msleep(10);
    
    return simulateMouseRelease(x, y, button);
}

void MouseSimulator::setLastError(const QString& error) {
    m_lastError = error;
}

bool MouseSimulator::isValidCoordinate(int x, int y) const {
    if (m_screenSize.isEmpty()) {
        return true; // 未知屏幕尺寸时不验证
    }
    return x >= 0 && x < m_screenSize.width() && 
           y >= 0 && y < m_screenSize.height();
}
