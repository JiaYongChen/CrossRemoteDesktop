#ifndef CLIPBOARDMANAGER_H
#define CLIPBOARDMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QString>

class QClipboard;

/**
 * @brief 剪贴板管理器类
 * 
 * 负责管理剪贴板同步功能，包括监听剪贴板变化、
 * 控制同步开关等功能。从ClientRemoteWindow中提取出来，
 * 实现单一职责原则。
 */
class ClipboardManager : public QObject
{
    Q_OBJECT
    
public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit ClipboardManager(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~ClipboardManager();
    
    /**
     * @brief 设置剪贴板同步是否启用
     * @param enabled 是否启用同步
     */
    void setSyncEnabled(bool enabled);
    
    /**
     * @brief 获取剪贴板同步是否启用
     * @return 是否启用同步
     */
    bool isSyncEnabled() const;
    
    /**
     * @brief 获取当前剪贴板文本内容
     * @return 剪贴板文本内容
     */
    QString currentText() const;
    
    /**
     * @brief 设置剪贴板文本内容
     * @param text 要设置的文本内容
     */
    void setText(const QString &text);
    
signals:
    /**
     * @brief 剪贴板内容发生变化时发出的信号
     * @param text 新的剪贴板文本内容
     */
    void clipboardChanged(const QString &text);
    
private slots:
    /**
     * @brief 处理系统剪贴板变化的槽函数
     */
    void onSystemClipboardChanged();
    
private:
    /**
     * @brief 初始化剪贴板连接
     */
    void setupConnections();
    
private:
    bool m_syncEnabled;              ///< 是否启用剪贴板同步
    QClipboard *m_clipboard;         ///< 系统剪贴板对象
    QString m_lastClipboardText;     ///< 上次的剪贴板文本，用于避免重复处理
};

#endif // CLIPBOARDMANAGER_H