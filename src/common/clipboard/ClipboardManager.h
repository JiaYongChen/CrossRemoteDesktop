#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QImage>

#include "common/network/Protocol.h"

/**
 * @brief 剪贴板管理器类
 *
 * 负责监听系统剪贴板变化并同步数据
 * 支持文本、图片和文件列表三种数据类型
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
     * @brief 重新发射当前剪贴板内容（认证成功后调用）
     *
     * 认证前发送闸门静默丢弃本地变化，但去重基线（m_lastText/m_lastImageData）已推进；
     * 不补发则闸门窗口内复制的内容永不同步到远端
     */
    void resync();

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

    /**
     * @brief 应用远端文件列表到本地剪贴板（存储元数据并防回环，不发射变化信号）
     * @param fileList 远端文件元数据列表
     */
    void applyRemoteFiles(const ClipboardFileList& fileList);

    /**
     * @brief 从 URL 列表提取本地文件元数据（静态工具函数，DragDropHandler 复用）
     * @param urls 剪贴板或拖放事件中的 URL 列表
     * @return 提取的文件列表（空列表表示无可传输文件）
     */
    static ClipboardFileList extractFiles(const QList<QUrl>& urls,
                                           QVector<QString>* outPaths = nullptr);

    /**
     * @brief 获取最近一次本地检测的文件列表（服务端响应文件数据请求用）
     * @return 文件列表（远端列表无本地路径时不适用）
     */
    ClipboardFileList lastFileList() const { return m_lastFileList; }

    /**
     * @brief 获取文件的完整路径（服务端响应文件数据请求用）
     * @param fileName 文件名
     * @return 完整路径；无记录返回空串
     */
    QString lastFilePath(int fileIndex) const {
        if (fileIndex >= 0 && fileIndex < m_lastFilePaths.size())
            return m_lastFilePaths.at(fileIndex);
        return {};
    }

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

    /**
     * @brief 本地剪贴板文件列表变化信号
     * @param fileList 文件元数据列表
     */
    void clipboardFilesChanged(const ClipboardFileList& fileList);

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

    /**
     * @brief 计算文件列表去重哈希（SHA-256，文件名+文件大小+修改时间拼接）
     * @param fileList 文件列表
     * @return 哈希值
     */
    static QByteArray computeFileListHash(const ClipboardFileList& fileList);

    QClipboard* m_clipboard;                ///< 系统剪贴板
    bool m_enabled;                         ///< 是否启用监听
    QString m_lastText;                     ///< 上次的文本内容
    QByteArray m_lastImageData;             ///< 上次的图片数据
    QString m_lastReceivedText;             ///< 上次从网络接收的文本（去重用）
    QImage m_lastReceivedImage;             ///< 上次从网络接收的图片（去重用，像素比对）
    QByteArray m_lastFileHash;              ///< 上次文件列表的去重哈希
    ClipboardFileList m_lastFileList;       ///< 上次的文件列表
    QVector<QString> m_lastFilePaths;        ///< 上次本地文件列表的路径（按 fileIndex 索引）
};
