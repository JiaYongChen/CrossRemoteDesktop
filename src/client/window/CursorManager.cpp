#include "CursorManager.h"
#include "common/core/network/Protocol.h"
#include "common/core/logging/LoggingCategories.h"

CursorManager::CursorManager(QObject* parent) : QObject(parent) {}

CursorManager::~CursorManager() = default;

void CursorManager::updateCursor(const CursorMessage& msg) {
    qCDebug(lcClientGL) << "CursorManager::updateCursor — w:" << msg.width
        << "h:" << msg.height << "hot:" << msg.hotX << "," << msg.hotY;
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
    m_dirty  = true;
}

void CursorManager::setCursorPosition(int x, int y) {
    m_lastX = x;
    m_lastY = y;
}

void CursorManager::setCursorEnabled(bool enabled) {
    m_enabled = enabled;
}
