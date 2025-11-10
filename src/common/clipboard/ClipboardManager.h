#ifndef CLIPBOARDMANAGER_H
#define CLIPBOARDMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtGui/QClipboard>
#include <QtGui/QImage>

class QClipboard;

/**
 * @brief 剪贴板管理器类
 * 
 * 负责监听系统剪贴板变化并同步数据
 * 支持文本和图片两种数据类型
 */
class ClipboardManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit ClipboardManager(QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~ClipboardManager() override;

    /**
     * @brief 启用剪贴板监听
     * @param enabled true=启用, false=禁用
     */
    void setEnabled(bool enabled);

    /**
     * @brief 获取启用状态
     */
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief 设置文本到剪贴板
     * @param text 文本内容
     */
    void setText(const QString& text);

    /**
     * @brief 设置图片到剪贴板
     * @param image 图片数据
     */
    void setImage(const QImage& image);

    /**
     * @brief 从 PNG 数据设置图片到剪贴板
     * @param pngData PNG 格式的图片数据
     */
    void setImageFromPng(const QByteArray& pngData);

signals:
    /**
     * @brief 本地剪贴板文本变化信号
     * @param text 新的文本内容
     */
    void clipboardTextChanged(const QString& text);

    /**
     * @brief 本地剪贴板图片变化信号
     * @param imageData PNG 格式的图片数据
     * @param width 图片宽度
     * @param height 图片高度
     */
    void clipboardImageChanged(const QByteArray& imageData, quint32 width, quint32 height);

private slots:
    /**
     * @brief 处理系统剪贴板变化
     * @param mode 变化模式
     */
    void onClipboardChanged(QClipboard::Mode mode);

private:
    QClipboard* m_clipboard;           ///< 系统剪贴板
    bool m_enabled;                    ///< 是否启用监听
    bool m_ignoreNextChange;           ///< 忽略下一次变化（自己触发的）
    QString m_lastText;                ///< 上次的文本内容
    QByteArray m_lastImageData;        ///< 上次的图片数据
};

#endif // CLIPBOARDMANAGER_H
