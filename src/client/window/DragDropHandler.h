#pragma once

#include <QtCore/QObject>
#include <QtCore/QUrl>

#include "common/network/Protocol.h"

class QDropEvent;
class QWidget;

/**
 * @brief 拖放处理器 — 处理远程桌面视口的文件拖入/拖出
 *
 * 事件过滤器安装在 ClientRemoteWindow 视口上：
 * - 拖入（本地文件 → 远端）：dragEnter 接受 URL 拖放，drop 时提取文件元数据并发射
 *   filesDroppedToRemote 信号
 * - 拖出（远端文件 → 本地）：startDragOut 以文件占位 URL 启动 QDrag（粘贴语义由
 *   文件请求通道完成）
 */
class DragDropHandler : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param viewport 目标视口（非拥有）
     * @param parent 父对象
     */
    explicit DragDropHandler(QWidget* viewport, QObject* parent = nullptr);

    /**
     * @brief 启动拖出（远端文件列表 → 本地占位）
     * @param fileList 远端文件元数据列表
     */
    void startDragOut(const ClipboardFileList& fileList);

    /**
     * @brief 从 URL 列表提取本地文件元数据（跳过非本地/不存在的文件，上限 MaxFileListCount）
     * @param urls URL 列表
     * @return 提取的文件列表
     */
    static ClipboardFileList extractFiles(const QList<QUrl>& urls);

signals:
    /**
     * @brief 本地文件拖入到远程视口
     * @param fileList 文件元数据列表
     */
    void filesDroppedToRemote(const ClipboardFileList& fileList);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* m_viewport;    ///< 目标视口（非拥有）
};
