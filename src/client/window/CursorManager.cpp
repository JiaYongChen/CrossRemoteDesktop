#include "CursorManager.h"
#include "common/core/network/Protocol.h"
#include "common/core/logging/LoggingCategories.h"

CursorManager::CursorManager(QObject* parent) : QObject(parent) {}

CursorManager::~CursorManager() = default;

void CursorManager::updateCursor(const CursorMessage& msg) {
    qCDebug(lcClientGL) << "CursorManager::updateCursor — w:" << msg.width
        << "h:" << msg.height << "hot:" << msg.hotX << "," << msg.hotY
        << "pos:" << msg.posX << "," << msg.posY;
    if (msg.width == 0 || msg.height == 0) {
        m_hasCursor = false;
        return;
    }
    m_hotX    = msg.hotX;
    m_hotY    = msg.hotY;
    m_width   = msg.width;
    m_height  = msg.height;
    m_pixels  = msg.pixels;
    m_remoteX = msg.posX;
    m_remoteY = msg.posY;
    m_hasCursor = true;
    m_dirty   = true;
}

void CursorManager::setCursorPosition(int x, int y) {
    m_localX = x;
    m_localY = y;
}

void CursorManager::setCursorEnabled(bool enabled) {
    m_enabled = enabled;
}

QPoint CursorManager::drawPos() const {
    int x = m_localX, y = m_localY;
    // 本地位置未更新时回退到服务端绝对坐标（映射到视口）
    if (m_localX == 0 && m_localY == 0 && m_remoteX > 0
        && m_remoteScreenSize.isValid() && m_viewportSize.isValid()) {
        x = m_remoteX * m_viewportSize.width()  / m_remoteScreenSize.width();
        y = m_remoteY * m_viewportSize.height() / m_remoteScreenSize.height();
    }
    return QPoint(x - m_hotX, y - m_hotY);
}
