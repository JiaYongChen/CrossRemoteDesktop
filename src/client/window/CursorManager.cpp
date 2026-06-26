#include "CursorManager.h"
#include "common/core/network/Protocol.h"
#include "common/core/logging/LoggingCategories.h"

CursorManager::CursorManager(QObject* parent) : QObject(parent) {}

CursorManager::~CursorManager() = default;

void CursorManager::updateCursor(const CursorMessage& msg) {
    // 光标隐藏
    if (msg.width == 0 || msg.height == 0) {
        if (m_hasCursor) {
            m_hasCursor = false;
            emit cursorChanged();
        }
        return;
    }

    // 检测实际变化（避免冗余 paintGL）
    const bool shapeChanged = (msg.hotX != m_hotX || msg.hotY != m_hotY
                            || msg.width != m_width || msg.height != m_height
                            || msg.pixels != m_pixels);
    const bool posChanged   = (msg.posX != m_remoteX || msg.posY != m_remoteY);
    const bool visibilityChanged = !m_hasCursor;

    m_hotX    = msg.hotX;
    m_hotY    = msg.hotY;
    m_width   = msg.width;
    m_height  = msg.height;
    m_pixels  = msg.pixels;
    m_remoteX = msg.posX;
    m_remoteY = msg.posY;
    m_hasCursor = true;

    if (visibilityChanged || shapeChanged || posChanged) {
        static int s_updateDiag = 0;
        if (++s_updateDiag <= 10)
            qCDebug(lcClientGL) << "CursorManager::updateCursor — w:" << msg.width
                << "h:" << msg.height << "hot:" << msg.hotX << "," << msg.hotY
                << "pos:" << msg.posX << "," << msg.posY;
        emit cursorChanged();
    }
}

void CursorManager::setCursorPosition(int x, int y) {
    m_localX = x;
    m_localY = y;
}

void CursorManager::setCursorEnabled(bool enabled) {
    m_enabled = enabled;
}

QPoint CursorManager::drawPos() const {
    // 始终使用服务端 DXGI 提取的权威光标位置，映射到客户端视口
    int x = 0, y = 0;
    if (m_remoteScreenSize.isValid() && m_viewportSize.isValid()) {
        x = m_remoteX * m_viewportSize.width()  / m_remoteScreenSize.width();
        y = m_remoteY * m_viewportSize.height() / m_remoteScreenSize.height();
    }
    return QPoint(x - m_hotX, y - m_hotY);
}
