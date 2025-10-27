#ifndef CURSORMANAGER_H
#define CURSORMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtGui/QPixmap>
#include <QtWidgets/QGraphicsRectItem>

class QGraphicsScene;
class QPainter;

/**
 * @brief 光标管理器类
 *
 * 负责管理远程桌面中的光标显示、位置和状态。
 * 提供光标的可见性控制、位置更新和自定义光标图像设置功能。
 */
class CursorManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param scene 图形场景，用于添加光标图形项
     * @param parent 父对象
     */
    explicit CursorManager(QGraphicsScene* scene, QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~CursorManager();

    /**
     * @brief 设置光标可见性
     * @param visible 是否可见
     */
    void setCursorVisible(bool visible);

    /**
     * @brief 获取光标可见性状态
     * @return 光标是否可见
     */
    bool isCursorVisible() const;

    /**
     * @brief 设置光标位置
     * @param position 光标位置
     */
    void setCursorPosition(const QPoint& position);

    /**
     * @brief 获取光标位置
     * @return 当前光标位置
     */
    QPoint cursorPosition() const;

    /**
     * @brief 设置自定义光标图像
     * @param pixmap 光标图像
     */
    void setCursorPixmap(const QPixmap& pixmap);

    /**
     * @brief 更新光标位置
     * @param position 新的光标位置
     */
    void updateCursorPosition(const QPoint& position);

    /**
     * @brief 绘制光标
     * @param painter 绘图对象
     */
    void drawCursor(QPainter& painter);

    /**
     * @brief 设置是否显示光标
     * @param show 是否显示
     */
    void setShowCursor(bool show);

    /**
     * @brief 获取是否显示光标
     * @return 是否显示光标
     */
    bool showCursor() const;

signals:
    /**
     * @brief 光标位置改变信号
     * @param position 新的光标位置
     */
    void cursorPositionChanged(const QPoint& position);

    /**
     * @brief 光标可见性改变信号
     * @param visible 新的可见性状态
     */
    void cursorVisibilityChanged(bool visible);

private:
    /**
     * @brief 初始化光标图形项
     */
    void ensureCursorItem();

private:
    QGraphicsScene* m_scene;           ///< 图形场景
    QGraphicsRectItem* m_cursorItem;   ///< 光标图形项
    bool m_cursorVisible;              ///< 光标可见性
    QPoint m_cursorPosition;           ///< 光标位置
    QPixmap m_cursorPixmap;            ///< 自定义光标图像
    bool m_showCursor;                 ///< 是否显示光标
};

#endif // CURSORMANAGER_H