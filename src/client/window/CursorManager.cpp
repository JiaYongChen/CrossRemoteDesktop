#include "CursorManager.h"
#include <QtWidgets/QWidget>
#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtCore/QDebug>

CursorManager::CursorManager(QWidget* targetWidget, QObject* parent)
    : QObject(parent)
    , m_targetWidget(targetWidget)
    , m_remoteCursorType(Qt::ArrowCursor) {  // 默认箭头光标
}

CursorManager::~CursorManager() {
    m_targetWidget = nullptr;
    // 恢复光标状态
    if ( m_targetWidget ) {
        restoreLocalCursor();
    }
}

// ==================== 本地光标控制 ====================

void CursorManager::applyLocalCursorState() {
    if ( !m_targetWidget ) {
        return;
    }

    // 根据远程光标类型设置本地光标
    m_targetWidget->setCursor(QCursor(m_remoteCursorType));

    // 如果有viewport（QGraphicsView的子类），也设置它的光标
    QWidget* viewport = m_targetWidget->findChild<QWidget*>("qt_scrollarea_viewport");
    if ( viewport ) {
        viewport->setCursor(QCursor(m_remoteCursorType));
    }
}

void CursorManager::restoreLocalCursor() {
    if ( !m_targetWidget ) {
        return;
    }

    // 恢复默认光标
    if ( m_targetWidget )
        m_targetWidget->unsetCursor();

    // 如果有viewport，也恢复它的光标
    QWidget* viewport = m_targetWidget->findChild<QWidget*>("qt_scrollarea_viewport");
    if ( viewport ) {
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
