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

    // 仅检测形状变化（位置由本地 InputForwarder 驱动，零延迟）
    const bool shapeChanged = (msg.hotX != m_hotX || msg.hotY != m_hotY
                            || msg.width != m_width || msg.height != m_height
                            || msg.pixels != m_pixels);
    const bool visibilityChanged = !m_hasCursor;
    // 远端位置变化 — 仅当无本地输入时有效（远端自发移动场景）
    const bool remotePosChanged = !m_hasLocalPos
                                  && (msg.posX != m_remoteX || msg.posY != m_remoteY);

    m_hotX    = msg.hotX;
    m_hotY    = msg.hotY;
    m_width   = msg.width;
    m_height  = msg.height;
    m_pixels  = msg.pixels;
    m_remoteX = msg.posX;  // 仅用于无本地输入时的回退
    m_remoteY = msg.posY;
    m_hasCursor = true;

    if (visibilityChanged || shapeChanged || remotePosChanged) {
        static int s_updateDiag = 0;
        if (++s_updateDiag <= 10)
            qCDebug(lcClientGL) << "CursorManager::updateCursor — shape w:" << msg.width
                << "h:" << msg.height << "hot:" << msg.hotX << "," << msg.hotY
                << (remotePosChanged ? " [remote pos]" : "");
        emit cursorChanged();
    }
}

void CursorManager::setCursorPosition(int x, int y) {
    if (x != m_localX || y != m_localY || !m_hasLocalPos) {
        m_localX = x;
        m_localY = y;
        m_hasLocalPos = true;
        if (m_hasCursor && m_enabled) {
            static int s_diag = 0;
            if (++s_diag <= 20)
                qCDebug(lcClientGL) << "setCursorPosition" << x << y << "→ emit cursorChanged";
            emit cursorChanged();
        } else {
            static int s_diag2 = 0;
            if (++s_diag2 <= 5)
                qCDebug(lcClientGL) << "setCursorPosition" << x << y
                    << "BLOCKED hasCursor=" << m_hasCursor << "enabled=" << m_enabled;
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
    // 远端回退：映射服务端屏幕坐标到客户端视口
    int x = 0, y = 0;
    if (m_remoteScreenSize.isValid() && m_viewportSize.isValid()) {
        x = m_remoteX * m_viewportSize.width()  / m_remoteScreenSize.width();
        y = m_remoteY * m_viewportSize.height() / m_remoteScreenSize.height();
    }
    return QPoint(x - m_hotX, y - m_hotY);
}
