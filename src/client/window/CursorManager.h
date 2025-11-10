#ifndef CURSORMANAGER_H
#define CURSORMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/Qt>
#include <QtGui/QPainter>

class QWidget;

/**
 * @brief 光标管理器类
 *
 * 统一管理本地光标的显示/隐藏和远程光标的显示
 * 提供简洁的接口用于远程桌面应用中的光标控制
 */
class CursorManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param targetWidget 目标窗口部件（用于设置光标）
     * @param parent 父对象
     */
    explicit CursorManager(QWidget* targetWidget, QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~CursorManager() override;

    // ==================== 本地光标控制 ====================

    /**
     * @brief 应用本地光标设置（在鼠标进入时调用）
     */
    void applyLocalCursorState();

    /**
     * @brief 恢复本地光标（在鼠标离开时调用）
     */
    void restoreLocalCursor();

    /**
     * @brief 强制刷新本地光标状态（在鼠标事件后调用）
     */
    void refreshLocalCursor();

    // ==================== 远程光标控制 ====================

    /**
     * @brief 设置远程光标类型
     * @param type 光标类型
     */
    void setRemoteCursorType(Qt::CursorShape type);

    /**
     * @brief 获取远程光标类型
     * @return 光标类型
     */
    Qt::CursorShape remoteCursorType() const;

    // ==================== 便捷方法 ====================

    /**
     * @brief 重置所有设置为默认值
     */
    void reset();

private:
    QWidget* m_targetWidget;          ///< 目标窗口部件

    // 远程光标状态
    Qt::CursorShape m_remoteCursorType; ///< 远程光标类型
};

#endif // CURSORMANAGER_H
