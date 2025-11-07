#include "InputSimulator.h"
#include "MouseSimulator.h"
#include "KeyboardSimulator.h"

// 平台特定的包含
#ifdef Q_OS_MACOS
#include "MouseSimulatorMacOS.h"
#include "KeyboardSimulatorMacOS.h"
#elif defined(Q_OS_WIN)
#include "MouseSimulatorWindows.h"
#include "KeyboardSimulatorWindows.h"
#elif defined(Q_OS_LINUX)
#include "MouseSimulatorLinux.h"
#include "KeyboardSimulatorLinux.h"
#endif

#include <QtCore/QMutexLocker>
#include <QtCore/QDebug>

InputSimulator::InputSimulator(QObject* parent)
    : QObject(parent)
    , m_initialized(false) {
    // 根据平台创建对应的模拟器实例
#ifdef Q_OS_MACOS
    m_mouseSimulator = std::make_unique<MouseSimulatorMacOS>();
    m_keyboardSimulator = std::make_unique<KeyboardSimulatorMacOS>();
#elif defined(Q_OS_WIN)
    m_mouseSimulator = std::make_unique<MouseSimulatorWindows>();
    m_keyboardSimulator = std::make_unique<KeyboardSimulatorWindows>();
#elif defined(Q_OS_LINUX)
    m_mouseSimulator = std::make_unique<MouseSimulatorLinux>();
    m_keyboardSimulator = std::make_unique<KeyboardSimulatorLinux>();
#else
    qWarning() << "InputSimulator: Unsupported platform";
#endif

    initialize();
}

InputSimulator::~InputSimulator() {
    cleanup();
}

bool InputSimulator::initialize() {
    QMutexLocker locker(&m_mutex);

    if ( m_initialized ) {
        return true;
    }

    bool mouseInit = false;
    bool keyboardInit = false;

    if ( m_mouseSimulator ) {
        mouseInit = m_mouseSimulator->initialize();
        if ( !mouseInit ) {
            setLastError("Failed to initialize mouse simulator: " + m_mouseSimulator->lastError());
        }
    }

    if ( m_keyboardSimulator ) {
        keyboardInit = m_keyboardSimulator->initialize();
        if ( !keyboardInit ) {
            setLastError("Failed to initialize keyboard simulator: " + m_keyboardSimulator->lastError());
        }
    }

    m_initialized = mouseInit && keyboardInit;

    if ( m_initialized ) {
        qDebug() << "InputSimulator: Initialized successfully";
    } else {
        qWarning() << "InputSimulator: Initialization failed:" << m_lastError;
    }

    return m_initialized;
}

void InputSimulator::cleanup() {
    QMutexLocker locker(&m_mutex);

    if ( m_mouseSimulator ) {
        m_mouseSimulator->cleanup();
    }

    if ( m_keyboardSimulator ) {
        m_keyboardSimulator->cleanup();
    }

    m_initialized = false;
}

bool InputSimulator::isInitialized() const {
    return m_initialized;
}

bool InputSimulator::simulateMouseMove(int x, int y) {
    QMutexLocker locker(&m_mutex);

    if ( !m_initialized || !m_mouseSimulator ) {
        setLastError("Mouse simulator not initialized");
        return false;
    }

    bool result = m_mouseSimulator->simulateMouseMove(x, y);
    if ( !result ) {
        setLastError(m_mouseSimulator->lastError());
    }

    return result;
}

bool InputSimulator::simulateMousePress(int x, int y, Qt::MouseButton button) {
    QMutexLocker locker(&m_mutex);

    if ( !m_initialized || !m_mouseSimulator ) {
        setLastError("Mouse simulator not initialized");
        return false;
    }

    bool result = m_mouseSimulator->simulateMousePress(x, y, button);
    if ( !result ) {
        setLastError(m_mouseSimulator->lastError());
    }

    return result;
}

bool InputSimulator::simulateMouseRelease(int x, int y, Qt::MouseButton button) {
    QMutexLocker locker(&m_mutex);

    if ( !m_initialized || !m_mouseSimulator ) {
        setLastError("Mouse simulator not initialized");
        return false;
    }

    bool result = m_mouseSimulator->simulateMouseRelease(x, y, button);
    if ( !result ) {
        setLastError(m_mouseSimulator->lastError());
    }

    return result;
}

bool InputSimulator::simulateMouseDoubleClick(int x, int y, Qt::MouseButton button) {
    QMutexLocker locker(&m_mutex);

    if ( !m_initialized || !m_mouseSimulator ) {
        setLastError("Mouse simulator not initialized");
        return false;
    }

    bool result = m_mouseSimulator->simulateMouseDoubleClick(x, y, button);
    if ( !result ) {
        setLastError(m_mouseSimulator->lastError());
    }

    return result;
}

bool InputSimulator::simulateMouseWheel(int x, int y, int delta) {
    QMutexLocker locker(&m_mutex);

    if ( !m_initialized || !m_mouseSimulator ) {
        setLastError("Mouse simulator not initialized");
        return false;
    }

    // 将单一 delta 转换为 deltaX=0, deltaY=delta
    bool result = m_mouseSimulator->simulateMouseWheel(x, y, 0, delta);
    if ( !result ) {
        setLastError(m_mouseSimulator->lastError());
    }

    return result;
}

bool InputSimulator::simulateKeyPress(int key, Qt::KeyboardModifiers modifiers) {
    QMutexLocker locker(&m_mutex);

    if ( !m_initialized || !m_keyboardSimulator ) {
        setLastError("Keyboard simulator not initialized");
        return false;
    }

    bool result = m_keyboardSimulator->simulateKeyPress(key, modifiers);
    if ( !result ) {
        setLastError(m_keyboardSimulator->lastError());
    }

    return result;
}

bool InputSimulator::simulateKeyRelease(int key, Qt::KeyboardModifiers modifiers) {
    QMutexLocker locker(&m_mutex);

    if ( !m_initialized || !m_keyboardSimulator ) {
        setLastError("Keyboard simulator not initialized");
        return false;
    }

    bool result = m_keyboardSimulator->simulateKeyRelease(key, modifiers);
    if ( !result ) {
        setLastError(m_keyboardSimulator->lastError());
    }

    return result;
}

QSize InputSimulator::getScreenSize() const {
    if ( !m_mouseSimulator ) {
        return QSize(0, 0);
    }

    return m_mouseSimulator->getScreenSize();
}

QPoint InputSimulator::getCursorPosition() const {
    if ( !m_mouseSimulator ) {
        return QPoint();
    }

    return m_mouseSimulator->getCursorPosition();
}

void InputSimulator::setEnabled(bool enabled) {
    QMutexLocker locker(&m_mutex);

    if ( m_mouseSimulator ) {
        m_mouseSimulator->setEnabled(enabled);
    }

    if ( m_keyboardSimulator ) {
        m_keyboardSimulator->setEnabled(enabled);
    }
}

bool InputSimulator::isEnabled() const {
    if ( m_mouseSimulator ) {
        return m_mouseSimulator->isEnabled();
    }

    if ( m_keyboardSimulator ) {
        return m_keyboardSimulator->isEnabled();
    }

    return false;
}

QString InputSimulator::lastError() const {
    return m_lastError;
}

void InputSimulator::setLastError(const QString& error) {
    m_lastError = error;
}
