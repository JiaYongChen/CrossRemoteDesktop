#include "DragDropHandler.h"

#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtGui/QDrag>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtWidgets/QWidget>

#include "common/clipboard/ClipboardManager.h"
#include "common/logging/LoggingCategories.h"

DragDropHandler::DragDropHandler(QWidget* viewport, QObject* parent)
    : QObject(parent)
    , m_viewport(viewport) {
}

void DragDropHandler::startDragOut(const ClipboardFileList& fileList) {
    if (!m_viewport) return;
    if (fileList.files.isEmpty()) return;

    // 文件占位 URL：远端文件在本机仅文件名（粘贴时按需请求数据）
    QMimeData* mimeData = new QMimeData;
    QList<QUrl> urls;
    urls.reserve(fileList.files.size());
    for (const ClipboardFileInfo& info : fileList.files) {
        urls.append(QUrl::fromLocalFile(info.fileName));
    }
    mimeData->setUrls(urls);

    QDrag* drag = new QDrag(m_viewport);
    drag->setMimeData(mimeData);
    drag->exec(Qt::CopyAction);

    qCDebug(lcClientRemoteWindow) << "拖出文件占位列表，条目数:" << fileList.files.size();
}

ClipboardFileList DragDropHandler::extractFiles(const QList<QUrl>& urls) {
    return ClipboardManager::extractFiles(urls);
}

bool DragDropHandler::eventFilter(QObject* watched, QEvent* event) {
    if (watched != m_viewport) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
        case QEvent::DragEnter: {
            auto* dragEnter = static_cast<QDragEnterEvent*>(event);
            if (dragEnter->mimeData() && dragEnter->mimeData()->hasUrls()) {
                dragEnter->acceptProposedAction();
            } else {
                dragEnter->ignore();
            }
            return true;
        }
        case QEvent::Drop: {
            auto* drop = static_cast<QDropEvent*>(event);
            if (drop->mimeData() && drop->mimeData()->hasUrls()) {
                const ClipboardFileList fileList = extractFiles(drop->mimeData()->urls());
                if (!fileList.files.isEmpty()) {
                    qCDebug(lcClientRemoteWindow) << "文件拖入远程视口，条目数:" << fileList.files.size();
                    emit filesDroppedToRemote(fileList);
                }
            }
            drop->setDropAction(Qt::CopyAction);
            drop->accept();
            return true;
        }
        default:
            break;
    }

    return QObject::eventFilter(watched, event);
}
