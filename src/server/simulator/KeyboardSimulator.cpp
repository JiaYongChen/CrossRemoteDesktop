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
    // 基本按键范围验证
    return key >= 0 && key <= 0xFFFF;
}
