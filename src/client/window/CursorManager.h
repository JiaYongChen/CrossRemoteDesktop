#pragma once

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QSize>
#include <QtCore/QByteArray>

struct CursorMessage;

class CursorManager : public QObject {
    Q_OBJECT
public:
    explicit CursorManager(QObject* parent = nullptr);
    ~CursorManager() override;

public slots:
    void updateCursor(const CursorMessage& msg);
    void setCursorPosition(int x, int y);
    void setCursorEnabled(bool enabled);

public:
    void setRemoteScreenSize(const QSize& sz) { m_remoteScreenSize = sz; }
    void setViewportSize(const QSize& sz)     { m_viewportSize = sz; }

    bool hasCursor() const { return m_hasCursor && m_enabled; }
    bool isDirty()    const { return m_dirty; }
    void clearDirty()       { m_dirty = false; }

    const QByteArray& pixels() const { return m_pixels; }
    int  width()  const { return m_width; }
    int  height() const { return m_height; }
    int  hotX()   const { return m_hotX; }
    int  hotY()   const { return m_hotY; }
    QPoint drawPos() const;

private:
    bool    m_enabled    = true;
    bool    m_hasCursor   = false;
    bool    m_dirty       = false;
    int     m_hotX       = 0, m_hotY       = 0;
    int     m_width      = 0, m_height     = 0;
    int     m_localX     = 0, m_localY    = 0;  // InputForwarder 本地坐标
    int     m_remoteX    = 0, m_remoteY   = 0;  // 服务端绝对坐标
    QSize   m_remoteScreenSize;   // 服务端桌面尺寸
    QSize   m_viewportSize;       // 客户端视口 widget 尺寸
    QByteArray m_pixels;
};
