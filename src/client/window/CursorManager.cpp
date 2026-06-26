#include "CursorManager.h"
#include "common/core/network/Protocol.h"
#include <QtGui/QPainter>

CursorManager::CursorManager(QObject* parent) : QObject(parent) {}

CursorManager::~CursorManager() = default;

void CursorManager::updateCursor(const CursorMessage& msg) {
    if (msg.width == 0 || msg.height == 0) {
        m_hasCursor = false;
        return;
    }
    m_hotX   = msg.hotX;
    m_hotY   = msg.hotY;
    m_width  = msg.width;
    m_height = msg.height;
    m_pixels = msg.pixels;
    m_hasCursor = true;
    // 懒创建 — 实际渲染时再转为 QImage
}

void CursorManager::setCursorPosition(int x, int y) {
    m_lastX = x;
    m_lastY = y;
}

void CursorManager::setCursorEnabled(bool enabled) {
    m_enabled = enabled;
}

QPoint CursorManager::cursorDrawPos() const {
    return QPoint(m_lastX - m_hotX, m_lastY - m_hotY);
}

void CursorManager::paintCursor(QPainter& painter) {
    if (!hasCursor()) return;
    if (m_cursorImage.isNull() || m_cursorImage.size() != QSize(m_width, m_height)) {
        m_cursorImage = QImage(
            reinterpret_cast<const uchar*>(m_pixels.constData()),
            m_width, m_height, QImage::Format_RGBA8888
        ).copy();  // 深拷贝，脱离 QByteArray 生命周期
    }
    painter.drawImage(cursorDrawPos(), m_cursorImage);
}
