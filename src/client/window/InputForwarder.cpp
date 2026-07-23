#include "InputForwarder.h"
#include "../session/ProtocolSession.h"
#include "CursorManager.h"
#include <QtWidgets/QWidget>
#include <QtGui/QMouseEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QWheelEvent>

#include "GLTextureViewport.h"

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
    if (m_viewport) {
        return m_viewport->mapToRemote(localPoint);
    }
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
        default:
            break;
    }
    return QObject::eventFilter(obj, event);
}

bool InputForwarder::handleKeyPress(QKeyEvent* event) {
    if (!m_protocolSession) return false;

    m_protocolSession->sendKeyboardEvent(event->key(),
        static_cast<int>(event->modifiers()), true, event->text());
    return false;
}

bool InputForwarder::handleKeyRelease(QKeyEvent* event) {
    if (!m_protocolSession) return false;

    m_protocolSession->sendKeyboardEvent(event->key(),
        static_cast<int>(event->modifiers()), false, QString());
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
    if (!m_protocolSession) return false;
    QPoint rp = mapToRemote(event->pos());
    int t = mouseEventTypeFromButton(event->button(), true);
    if (t != 0) {
        m_protocolSession->sendMouseEvent(rp.x(), rp.y(), t);
    }
    if (m_cursorManager) {
        m_cursorManager->setCursorPosition(event->pos().x(), event->pos().y());
    }
    return false;
}

bool InputForwarder::handleMouseRelease(QMouseEvent* event) {
    if (!m_protocolSession) return false;
    QPoint rp = mapToRemote(event->pos());
    int t = mouseEventTypeFromButton(event->button(), false);
    if (t != 0) {
        m_protocolSession->sendMouseEvent(rp.x(), rp.y(), t);
    }
    if (m_cursorManager) {
        m_cursorManager->setCursorPosition(event->pos().x(), event->pos().y());
    }
    return false;
}

bool InputForwarder::handleMouseMove(QMouseEvent* event) {
    if (!m_protocolSession) return false;
    QPoint rp = mapToRemote(event->pos());
    m_protocolSession->sendMouseEvent(rp.x(), rp.y(),
        static_cast<int>(MouseEventType::MOVE));
    if (m_cursorManager) {
        m_cursorManager->setCursorPosition(event->pos().x(), event->pos().y());
    }
    return false;
}

bool InputForwarder::handleMouseDoubleClick(QMouseEvent* event) {
    if (!m_protocolSession) return false;
    QPoint rp = mapToRemote(event->pos());
    int t = mouseEventTypeFromButtonDbl(event->button());
    if (t != 0) {
        m_protocolSession->sendMouseEvent(rp.x(), rp.y(), t);
    }
    if (m_cursorManager) {
        m_cursorManager->setCursorPosition(event->pos().x(), event->pos().y());
    }
    return false;
}

bool InputForwarder::handleWheel(QWheelEvent* event) {
    if (!m_protocolSession) return false;
    QPoint rp = mapToRemote(event->position().toPoint());
    int delta = event->angleDelta().y();
    m_protocolSession->sendWheelEvent(rp.x(), rp.y(), delta, Qt::Vertical);
    return false;
}
