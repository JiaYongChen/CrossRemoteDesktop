#include "cursormanager.h"
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsRectItem>
#include <QtGui/QPainter>
#include <QtCore/QDebug>

CursorManager::CursorManager(QGraphicsScene *scene, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
    , m_cursorItem(nullptr)
    , m_cursorVisible(true)
    , m_cursorPosition(0, 0)
    , m_showCursor(true)
{
    // 延迟初始化光标项，避免在构造函数中访问可能未完全初始化的scene
}

CursorManager::~CursorManager()
{
    if (m_cursorItem && m_scene) {
        m_scene->removeItem(m_cursorItem);
        delete m_cursorItem;
    }
}

void CursorManager::setCursorVisible(bool visible)
{
    if (m_cursorVisible != visible) {
        m_cursorVisible = visible;
        
        ensureCursorItem();
        if (m_cursorItem) {
            m_cursorItem->setVisible(visible && m_showCursor);
        }
        
        emit cursorVisibilityChanged(visible);
    }
}

bool CursorManager::isCursorVisible() const
{
    return m_cursorVisible;
}

void CursorManager::setCursorPosition(const QPoint &position)
{
    if (m_cursorPosition != position) {
        m_cursorPosition = position;
        
        ensureCursorItem();
        if (m_cursorItem) {
            m_cursorItem->setPos(position);
        }
        
        emit cursorPositionChanged(position);
    }
}

QPoint CursorManager::cursorPosition() const
{
    return m_cursorPosition;
}

void CursorManager::setCursorPixmap(const QPixmap &pixmap)
{
    m_cursorPixmap = pixmap;
    
    ensureCursorItem();
    // 如果有自定义光标图像，可以在这里更新光标显示
    // 目前使用简单的矩形表示光标
}

void CursorManager::updateCursorPosition(const QPoint &position)
{
    setCursorPosition(position);
}

void CursorManager::drawCursor(QPainter &painter)
{
    if (!m_cursorVisible || !m_showCursor) {
        return;
    }
    
    // 保存当前画笔状态
    QPen oldPen = painter.pen();
    QBrush oldBrush = painter.brush();
    
    // 设置光标绘制样式
    painter.setPen(QPen(Qt::white, 2));
    painter.setBrush(QBrush(Qt::black));
    
    // 绘制光标（简单的十字形）
    const int cursorSize = 10;
    const QPoint &pos = m_cursorPosition;
    
    // 绘制垂直线
    painter.drawLine(pos.x(), pos.y() - cursorSize/2, 
                    pos.x(), pos.y() + cursorSize/2);
    
    // 绘制水平线
    painter.drawLine(pos.x() - cursorSize/2, pos.y(), 
                    pos.x() + cursorSize/2, pos.y());
    
    // 如果有自定义光标图像，绘制它
    if (!m_cursorPixmap.isNull()) {
        painter.drawPixmap(pos.x() - m_cursorPixmap.width()/2, 
                          pos.y() - m_cursorPixmap.height()/2, 
                          m_cursorPixmap);
    }
    
    // 恢复画笔状态
    painter.setPen(oldPen);
    painter.setBrush(oldBrush);
}

void CursorManager::setShowCursor(bool show)
{
    if (m_showCursor != show) {
        m_showCursor = show;
        
        ensureCursorItem();
        if (m_cursorItem) {
            m_cursorItem->setVisible(m_cursorVisible && show);
        }
    }
}

bool CursorManager::showCursor() const
{
    return m_showCursor;
}

void CursorManager::ensureCursorItem()
{
    if (m_cursorItem || !m_scene) {
        return;
    }
    
    // 创建光标图形项（简单的矩形表示）
    m_cursorItem = new QGraphicsRectItem(-2, -2, 4, 4);
    m_cursorItem->setPen(QPen(Qt::white, 1));
    m_cursorItem->setBrush(QBrush(Qt::black));
    m_cursorItem->setPos(m_cursorPosition);
    m_cursorItem->setVisible(m_cursorVisible && m_showCursor);
    m_cursorItem->setZValue(1000); // 确保光标在最上层
    
    m_scene->addItem(m_cursorItem);
}