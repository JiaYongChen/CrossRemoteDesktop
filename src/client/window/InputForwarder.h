#pragma once

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QRect>

class QWidget;
class QMouseEvent;
class QKeyEvent;
class QWheelEvent;
class QFocusEvent;
class QEnterEvent;
class QEvent;
class ProtocolSession;
class GLTextureViewport;

/// 输入事件转发器 — 从 ClientRemoteWindow 分离出的输入处理职责
///
/// 安装为 QWidget 的事件过滤器，拦截所有 mouse/keyboard/wheel
/// 事件，通过 SessionManager::sendMouseEvent/sendKeyboardEvent
/// 跨线程转发到服务端。
class InputForwarder : public QObject {
    Q_OBJECT
public:
    explicit InputForwarder(QObject* parent = nullptr);

    /// 安装到目标窗口（作为事件过滤器）
    void installOn(QWidget* target);

    /// 设置 ProtocolSession（用于事件转发）
    void setProtocolSession(ProtocolSession* sm) { m_protocolSession = sm; }

    /// 设置坐标映射提供方（GLTextureViewport）
    void setViewport(GLTextureViewport* vp) { m_viewport = vp; }

    /// 启用/禁用输入转发
    void setEnabled(bool enabled) { m_enabled = enabled; }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QPoint mapToRemote(const QPoint& localPoint) const;

    // ── 事件处理器 ──
    bool handleKeyPress(QKeyEvent* event);
    bool handleKeyRelease(QKeyEvent* event);
    bool handleMousePress(QMouseEvent* event);
    bool handleMouseRelease(QMouseEvent* event);
    bool handleMouseMove(QMouseEvent* event);
    bool handleMouseDoubleClick(QMouseEvent* event);
    bool handleWheel(QWheelEvent* event);
    void handleEnter(QEnterEvent* event);
    void handleLeave(QEvent* event);

    ProtocolSession* m_protocolSession = nullptr;
    GLTextureViewport* m_viewport = nullptr;
    bool m_enabled = true;
    QPoint m_lastMousePos{-1, -1};
};
