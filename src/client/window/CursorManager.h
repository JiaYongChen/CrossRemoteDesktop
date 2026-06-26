#pragma once

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QByteArray>
#include <QtGui/QImage>
#include <QtOpenGL/QOpenGLFunctions_3_3_Core>

struct CursorMessage; // 前置声明

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
    bool hasCursor() const { return m_hasCursor && m_enabled; }
    QPoint cursorDrawPos() const;   // 渲染矩形左上角
    void paintCursor(QPainter& painter);
    int hotspotX() const { return m_hotX; }
    int hotspotY() const { return m_hotY; }

private:
    void buildTexture();

    bool    m_enabled    = true;
    bool    m_hasCursor   = false;
    int     m_hotX       = 0, m_hotY       = 0;
    int     m_width      = 0, m_height     = 0;
    int     m_lastX      = 0, m_lastY      = 0;
    QImage  m_cursorImage;
    QByteArray m_pixels;
};
