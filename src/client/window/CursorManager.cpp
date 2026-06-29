#include "CursorManager.h"
#include "common/core/network/Protocol.h"
#include "common/core/logging/LoggingCategories.h"

CursorManager::CursorManager(QObject* parent) : QObject(parent) {}

CursorManager::~CursorManager() = default;

void CursorManager::updateCursor(const CursorMessage& msg) {
    if (msg.width == 0 || msg.height == 0) {
        return;  // 忽略无效消息，不重置已有形状
    }

    const bool shapeChanged = (msg.hotX != m_hotX || msg.hotY != m_hotY
                            || msg.width != m_width || msg.height != m_height
                            || msg.pixels != m_pixels);
    const bool visibilityChanged = !m_hasCursor;
    const bool remotePosChanged = !m_hasLocalPos
                                  && (msg.posX != m_remoteX || msg.posY != m_remoteY);

    m_hotX    = msg.hotX;
    m_hotY    = msg.hotY;
    m_width   = msg.width;
    m_height  = msg.height;
    m_pixels  = msg.pixels;
    m_remoteX = msg.posX;
    m_remoteY = msg.posY;
    m_hasCursor = true;

    if (visibilityChanged || shapeChanged || remotePosChanged) {
        emit cursorChanged();
    }
}

void CursorManager::setCursorPosition(int x, int y) {
    if (x != m_localX || y != m_localY || !m_hasLocalPos) {
        m_localX = x;
        m_localY = y;
        m_hasLocalPos = true;
        if (m_enabled) {
            emit cursorChanged();
        }
    }
}

void CursorManager::setCursorEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        m_hasLocalPos = false;  // 移出窗口，下次移入时从远端位置回退开始
    }
}

QPoint CursorManager::drawPos() const {
    // 本地位置优先：InputForwarder 提供的客户端鼠标坐标（零延迟）
    // 回退到服务端远端坐标：当用户未触碰本地鼠标时（如观看他人操作服务端）
    if (m_hasLocalPos) {
        return QPoint(m_localX - m_hotX, m_localY - m_hotY);
    }
    // 远端回退：映射服务端屏幕坐标到客户端渲染矩形（含 letterbox/pillarbox 偏移）
    int x = 0, y = 0;
    if (m_remoteScreenSize.isValid() && m_renderRect.isValid()) {
        x = static_cast<int>(m_remoteX * m_renderRect.width()  / m_remoteScreenSize.width()
                             + m_renderRect.x());
        y = static_cast<int>(m_remoteY * m_renderRect.height() / m_remoteScreenSize.height()
                             + m_renderRect.y());
    }
    return QPoint(x - m_hotX, y - m_hotY);
}
