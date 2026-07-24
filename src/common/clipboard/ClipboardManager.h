#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtGui/QClipboard>
#include <QtGui/QImage>

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
     * @brief 设置文本到剪贴板
     * @param text 文本内容
     */
    void setText(const QString& text);

    /**
     * @brief 从 PNG 数据设置图片到剪贴板
     * @param pngData PNG 格式的图片数据
     */
    void setImageFromPng(const QByteArray& pngData);

    /**
     * @brief 应用远端文本到本地剪贴板（设置去重标记，不发射变化信号）
     * @param text 远端文本内容
     */
    void applyRemoteText(const QString& text);

    /**
     * @brief 应用远端图片到本地剪贴板（设置去重标记，不发射变化信号）
     * @param pngData PNG 格式的图片数据
     */
    void applyRemoteImage(const QByteArray& pngData);

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
    /**
     * @brief 设置图片到剪贴板（仅由 setImageFromPng 内部使用）
     * @param image 图片数据
     */
    void setImage(const QImage& image);

    QClipboard* m_clipboard;                ///< 系统剪贴板
    bool m_enabled;                         ///< 是否启用监听
    QString m_lastText;                     ///< 上次的文本内容
    QByteArray m_lastImageData;             ///< 上次的图片数据
    QString m_lastReceivedText;             ///< 上次从网络接收的文本（去重用）
    QImage m_lastReceivedImage;             ///< 上次从网络接收的图片（去重用，像素比对）
};
