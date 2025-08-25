#ifndef FILETRANSFERMANAGER_H
#define FILETRANSFERMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QPoint>

class QWidget;
class QDragEnterEvent;
class QDropEvent;

/**
 * @brief 文件传输管理器类
 * 
 * 负责管理拖拽文件传输功能，包括处理拖拽进入事件、
 * 文件放置事件等。从ClientRemoteWindow中提取出来，
 * 实现单一职责原则。
 */
class FileTransferManager : public QObject
{
    Q_OBJECT
    
public:
    /**
     * @brief 构造函数
     * @param targetWidget 接收拖拽事件的目标控件
     * @param parent 父对象
     */
    explicit FileTransferManager(QWidget *targetWidget, QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~FileTransferManager();
    
    /**
     * @brief 设置文件传输是否启用
     * @param enabled 是否启用文件传输
     */
    void setEnabled(bool enabled);
    
    /**
     * @brief 获取文件传输是否启用
     * @return 是否启用文件传输
     */
    bool isEnabled() const;
    
    /**
     * @brief 设置目标控件
     * @param widget 新的目标控件
     */
    void setTargetWidget(QWidget *widget);
    
    /**
     * @brief 获取目标控件
     * @return 当前目标控件
     */
    QWidget* targetWidget() const;
    
    /**
     * @brief 处理拖拽进入事件
     * @param event 拖拽进入事件
     * @return 是否接受该事件
     */
    bool handleDragEnterEvent(QDragEnterEvent *event);
    
    /**
     * @brief 处理文件放置事件
     * @param event 文件放置事件
     * @return 是否接受该事件
     */
    bool handleDropEvent(QDropEvent *event);
    
signals:
    /**
     * @brief 文件被放置时发出的信号
     * @param files 被放置的文件路径列表
     * @param x 放置位置的X坐标
     * @param y 放置位置的Y坐标
     */
    void filesDropped(const QStringList &files, int x, int y);
    
    /**
     * @brief 文件传输状态变化信号
     * @param enabled 新的启用状态
     */
    void enabledChanged(bool enabled);
    
private:
    /**
     * @brief 初始化目标控件的拖拽设置
     */
    void setupTargetWidget();
    
    /**
     * @brief 从URL列表中提取本地文件路径
     * @param urls URL列表
     * @return 本地文件路径列表
     */
    QStringList extractLocalFiles(const QList<QUrl> &urls) const;
    
private:
    bool m_enabled;              ///< 是否启用文件传输
    QWidget *m_targetWidget;     ///< 接收拖拽事件的目标控件
};

#endif // FILETRANSFERMANAGER_H