#include "CursorManager.h"
#include <QtWidgets/QAbstractScrollArea>
#include <QtWidgets/QWidget>
#include <QtGui/QPainter>
#include <QtGui/QPen>

CursorManager::CursorManager(QWidget* targetWidget, QObject* parent)
    : QObject(parent)
    , m_targetWidget(targetWidget)
    , m_remoteCursorType(Qt::ArrowCursor) {  // 默认箭头光标
}

CursorManager::~CursorManager() {
    // 关键：析构期间绝不访问 m_targetWidget 指向的 widget。
    //
    // 原因（Qt 6.9 实现细节）：CursorManager 作为 QObject 子对象，
    // 被 ~QWidget() 的 d->deleteChildren() 提前删除（qtbase/src/widgets/kernel/qwidget.cpp
    // 第 1572-1573 行），此时：
    //   1) 父 widget 的派生类（QGraphicsView/QAbstractScrollArea/QFrame）body 已析构完，
    //      vtable 已降级到 QWidget，qobject_cast 到派生类会返回 nullptr；
    //   2) ~QObject() 尚未执行，QPointer 的 sharedRefcount->strongref 仍为 -1，
    //      所以 QPointer::isNull() 仍返回 false（"对象还活着"），
    //      但实际派生类状态已不可用；
    //   3) 任何对 target 的 findChild 等调用都可能命中半析构路径并触发
    //      qobject.cpp 的 Q_ASSERT(parent)。
    //
    // Widget 即将销毁，无需恢复光标；直接清空 QPointer 即可。
    m_targetWidget.clear();
}

// ==================== 本地光标控制 ====================

void CursorManager::applyLocalCursorState() {
    if ( !m_targetWidget ) {
        return;
    }

    // 根据远程光标类型设置本地光标
    m_targetWidget->setCursor(QCursor(m_remoteCursorType));

    // 若存在 viewport（QAbstractScrollArea 的子类），也设置它的光标
    if ( QWidget* viewport = resolveViewport() ) {
        viewport->setCursor(QCursor(m_remoteCursorType));
    }
}

void CursorManager::restoreLocalCursor() {
    if ( !m_targetWidget ) {
        return;
    }

    // 恢复默认光标
    m_targetWidget->unsetCursor();

    // 若存在 viewport，也恢复它的光标
    if ( QWidget* viewport = resolveViewport() ) {
        viewport->unsetCursor();
    }
}

void CursorManager::refreshLocalCursor() {
    if ( !m_targetWidget ) {
        return;
    }

    // 在鼠标事件后重新应用光标状态
    // 某些Qt内部操作可能会重置光标
    applyLocalCursorState();
}

// ==================== 远程光标控制 ====================

void CursorManager::setRemoteCursorType(Qt::CursorShape type) {
    if ( m_remoteCursorType == type ) {
        return;
    }

    m_remoteCursorType = type;

    // 更新本地光标显示
    applyLocalCursorState();
}

Qt::CursorShape CursorManager::remoteCursorType() const {
    return m_remoteCursorType;
}

// ==================== 便捷方法 ====================

void CursorManager::reset() {
    m_remoteCursorType = Qt::ArrowCursor;

    restoreLocalCursor();

    if ( m_targetWidget ) {
        m_targetWidget->update();
    }
}

// ==================== 内部辅助 ====================

QWidget* CursorManager::resolveViewport() const {
    if ( !m_targetWidget ) {
        return nullptr;
    }

    // target 恒为 QGraphicsView（QAbstractScrollArea 派生）。qobject_cast 只在
    // 对象活着且完整时成功；若失败（返回 nullptr），说明 target 正处于派生类
    // 析构阶段（vtable 已降级），此时绝对不能再对它做任何派生类方法调用，
    // 更不能调用 findChild——这会命中 qobject.cpp:2190 的 Q_ASSERT(parent)。
    auto* scrollArea = qobject_cast<QAbstractScrollArea*>(m_targetWidget.data());
    if ( !scrollArea ) {
        return nullptr;
    }
    return scrollArea->viewport();
}
