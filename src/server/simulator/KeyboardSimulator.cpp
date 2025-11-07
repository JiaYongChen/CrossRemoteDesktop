#include "KeyboardSimulator.h"

KeyboardSimulator::KeyboardSimulator(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_enabled(true)
{
}

KeyboardSimulator::~KeyboardSimulator() {
}

void KeyboardSimulator::setLastError(const QString& error) {
    m_lastError = error;
}

bool KeyboardSimulator::isValidKey(int key) const {
    // Qt 键码范围验证
    // Qt::Key 枚举值范围:
    // - 标准 ASCII 字符: 0x20 - 0x7E
    // - 特殊键: 0x01000000 - 0x01FFFFFF (如 Qt::Key_Backspace = 0x01000003)
    // - 小键盘修饰符: 0x20000000
    // 因此我们需要接受更大的范围
    
    if (key < 0) {
        return false;
    }
    
    // 移除小键盘修饰符标志后检查
    int baseKey = key & ~0x20000000;
    
    // 允许的范围:
    // 1. ASCII 可打印字符和控制字符: 0x00 - 0xFF
    // 2. Qt 特殊键: 0x01000000 - 0x01FFFFFF
    // 3. 多媒体键等扩展键: 0x010000FF - 0x0100FFFF
    return (baseKey <= 0xFF) || 
           (baseKey >= 0x01000000 && baseKey <= 0x01FFFFFF);
}
