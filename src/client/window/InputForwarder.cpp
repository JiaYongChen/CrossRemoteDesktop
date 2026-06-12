#include "InputForwarder.h"
#include "../managers/SessionManager.h"
#include <QtWidgets/QWidget>
#include <QtGui/QMouseEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QFocusEvent>
#include <QtGui/QEnterEvent>

#ifndef QT_NO_OPENGL
#include "GLTextureViewport.h"
#endif

InputForwarder::InputForwarder(QObject* parent)
    : QObject(parent) {
}

void InputForwarder::installOn(QWidget* target) {
    if (target) {
        target->installEventFilter(this);
        target->setMouseTracking(true);
    }
}

QPoint InputForwarder::mapToRemote(const QPoint& localPoint) const {
#ifndef QT_NO_OPENGL
    if (m_viewport) {
        return m_viewport->mapToRemote(localPoint);
    }
#else
    Q_UNUSED(localPoint)
#endif
    return QPoint(0, 0);
}

bool InputForwarder::eventFilter(QObject* obj, QEvent* event) {
    if (!m_enabled) {
        return QObject::eventFilter(obj, event);
    }

    switch (event->type()) {
        case QEvent::KeyPress:
            return handleKeyPress(static_cast<QKeyEvent*>(event));
        case QEvent::KeyRelease:
            return handleKeyRelease(static_cast<QKeyEvent*>(event));
        case QEvent::MouseButtonPress:
            return handleMousePress(static_cast<QMouseEvent*>(event));
        case QEvent::MouseButtonRelease:
            return handleMouseRelease(static_cast<QMouseEvent*>(event));
        case QEvent::MouseMove:
            return handleMouseMove(static_cast<QMouseEvent*>(event));
        case QEvent::MouseButtonDblClick:
            return handleMouseDoubleClick(static_cast<QMouseEvent*>(event));
        case QEvent::Wheel:
            return handleWheel(static_cast<QWheelEvent*>(event));
        case QEvent::Enter:
            handleEnter(static_cast<QEnterEvent*>(event));
            break;
        case QEvent::Leave:
            handleLeave(event);
            break;
        default:
            break;
    }
    return QObject::eventFilter(obj, event);
}

bool InputForwarder::handleKeyPress(QKeyEvent* event) {
    if (!m_sessionManager) return false;

#ifndef QT_NO_OPENGL
    // Toggle VSync with Ctrl+V (GL mode only)
    if (event->key() == Qt::Key_V && (event->modifiers() & Qt::ControlModifier)) {
        if (m_viewport) {
            m_viewport->setVSyncEnabled(!m_viewport->isVSyncEnabled());
            event->accept();
            return true;
        }
    }
#endif

    QMetaObject::invokeMethod(m_sessionManager, "sendKeyboardEvent",
        Qt::QueuedConnection,
        Q_ARG(int, event->key()),
        Q_ARG(int, static_cast<int>(event->modifiers())),
        Q_ARG(bool, true),
        Q_ARG(QString, event->text()));
    return false;
}

bool InputForwarder::handleKeyRelease(QKeyEvent* event) {
    if (!m_sessionManager) return false;

    QMetaObject::invokeMethod(m_sessionManager, "sendKeyboardEvent",
        Qt::QueuedConnection,
        Q_ARG(int, event->key()),
        Q_ARG(int, static_cast<int>(event->modifiers())),
        Q_ARG(bool, false),
        Q_ARG(QString, QString()));
    return false;
}

static int mouseEventTypeFromButton(Qt::MouseButton btn, bool press) {
    using MET = MouseEventType;
    if (btn == Qt::LeftButton)   return static_cast<int>(press ? MET::LEFT_PRESS   : MET::LEFT_RELEASE);
    if (btn == Qt::RightButton)  return static_cast<int>(press ? MET::RIGHT_PRESS  : MET::RIGHT_RELEASE);
    if (btn == Qt::MiddleButton) return static_cast<int>(press ? MET::MIDDLE_PRESS : MET::MIDDLE_RELEASE);
    return 0;
}

static int mouseEventTypeFromButtonDbl(Qt::MouseButton btn) {
    using MET = MouseEventType;
    if (btn == Qt::LeftButton)   return static_cast<int>(MET::LEFT_DOUBLE_CLICK);
    if (btn == Qt::RightButton)  return static_cast<int>(MET::RIGHT_DOUBLE_CLICK);
    if (btn == Qt::MiddleButton) return static_cast<int>(MET::MIDDLE_DOUBLE_CLICK);
    return 0;
}

bool InputForwarder::handleMousePress(QMouseEvent* event) {
    if (!m_sessionManager) return false;
    QPoint rp = mapToRemote(event->pos());
    int t = mouseEventTypeFromButton(event->button(), true);
    if (t != 0) {
        QMetaObject::invokeMethod(m_sessionManager, "sendMouseEvent",
            Qt::QueuedConnection, Q_ARG(int, rp.x()), Q_ARG(int, rp.y()), Q_ARG(int, t));
    }
    m_lastMousePos = event->pos();
    return false;
}

bool InputForwarder::handleMouseRelease(QMouseEvent* event) {
    if (!m_sessionManager) return false;
    QPoint rp = mapToRemote(event->pos());
    int t = mouseEventTypeFromButton(event->button(), false);
    if (t != 0) {
        QMetaObject::invokeMethod(m_sessionManager, "sendMouseEvent",
            Qt::QueuedConnection, Q_ARG(int, rp.x()), Q_ARG(int, rp.y()), Q_ARG(int, t));
    }
    return false;
}

bool InputForwarder::handleMouseMove(QMouseEvent* event) {
    if (!m_sessionManager) return false;
    QPoint rp = mapToRemote(event->pos());
    QMetaObject::invokeMethod(m_sessionManager, "sendMouseEvent",
        Qt::QueuedConnection, Q_ARG(int, rp.x()), Q_ARG(int, rp.y()),
        Q_ARG(int, static_cast<int>(MouseEventType::MOVE)));
    m_lastMousePos = event->pos();
    return false;
}

bool InputForwarder::handleMouseDoubleClick(QMouseEvent* event) {
    if (!m_sessionManager) return false;
    QPoint rp = mapToRemote(event->pos());
    int t = mouseEventTypeFromButtonDbl(event->button());
    if (t != 0) {
        QMetaObject::invokeMethod(m_sessionManager, "sendMouseEvent",
            Qt::QueuedConnection, Q_ARG(int, rp.x()), Q_ARG(int, rp.y()), Q_ARG(int, t));
    }
    return false;
}

bool InputForwarder::handleWheel(QWheelEvent* event) {
    if (!m_sessionManager) return false;
    QPoint rp = mapToRemote(event->position().toPoint());
    int delta = event->angleDelta().y();
    QMetaObject::invokeMethod(m_sessionManager, "sendWheelEvent",
        Qt::QueuedConnection, Q_ARG(int, rp.x()), Q_ARG(int, rp.y()),
        Q_ARG(int, delta), Q_ARG(int, Qt::Vertical));
    return false;
}

void InputForwarder::handleEnter(QEnterEvent* event) {
    Q_UNUSED(event);
    // CursorManager integration is handled by ClientRemoteWindow
}

void InputForwarder::handleLeave(QEvent* event) {
    Q_UNUSED(event);
    // CursorManager integration is handled by ClientRemoteWindow
}
