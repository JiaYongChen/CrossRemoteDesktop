#pragma once

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QSize>
#include <QtCore/QRectF>
#include <QtCore/QByteArray>

struct CursorMessage;

class CursorManager : public QObject {
    Q_OBJECT
public:
    explicit CursorManager(QObject* parent = nullptr);
    ~CursorManager() override;

signals:
    /// 光标状态变更（位置/形状），触发 GL 视口立即重绘
    void cursorChanged();

public slots:
    void updateCursor(const CursorMessage& msg);
    void setCursorPosition(int x, int y);
    void setCursorEnabled(bool enabled);
    /// 清除本地位置标记，使 drawPos() 回退到远端坐标（鼠标离开窗口时调用）
    void clearLocalPosition();

public:
    void setRemoteScreenSize(const QSize& sz) { m_remoteScreenSize = sz; }
    void setViewportSize(const QSize& sz)     { m_viewportSize = sz; }
    /// 同步渲染矩形（含 letterbox/pillarbox 偏移），用于远端坐标映射
    void setRenderRect(const QRectF& rect)    { m_renderRect = rect; }

    bool hasCursor() const { return m_hasCursor && m_enabled; }

    const QByteArray& pixels() const { return m_pixels; }
    int  width()  const { return m_width; }
    int  height() const { return m_height; }
    QPoint drawPos() const;

private:
    bool    m_enabled      = true;
    bool    m_hasCursor     = false;
    bool    m_hasLocalPos   = false;  // InputForwarder 是否已上报过本地鼠标位置
    int     m_hotX         = 0, m_hotY         = 0;
    int     m_width        = 0, m_height       = 0;
    int     m_localX       = 0, m_localY      = 0;  // InputForwarder 本地坐标（视口空间）
    int     m_remoteX      = 0, m_remoteY     = 0;  // 服务端绝对坐标
    QSize   m_remoteScreenSize;   // 服务端桌面尺寸
    QSize   m_viewportSize;       // 客户端视口 widget 尺寸（仅供参考）
    QRectF  m_renderRect;         // GL 渲染矩形（含 letterbox/pillarbox 偏移）
    QByteArray m_pixels;
};
