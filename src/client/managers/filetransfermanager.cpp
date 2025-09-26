#include "FileTransferManager.h"
#include <QtWidgets/QWidget>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtCore/QMimeData>
#include <QtCore/QUrl>
#include <QtCore/QFileInfo>
#include <QtCore/QDebug>

FileTransferManager::FileTransferManager(QWidget *targetWidget, QObject *parent)
    : QObject(parent)
    , m_enabled(true)
    , m_targetWidget(targetWidget)
{
    setupTargetWidget();
}

FileTransferManager::~FileTransferManager()
{
    // Qt对象会自动清理，无需手动删除
}

void FileTransferManager::setupTargetWidget()
{
    if (m_targetWidget) {
        // 启用拖拽接受
        m_targetWidget->setAcceptDrops(true);
        qDebug() << "FileTransferManager: Target widget drag and drop enabled";
    }
}

void FileTransferManager::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        
        // 更新目标控件的拖拽接受状态
        if (m_targetWidget) {
            m_targetWidget->setAcceptDrops(enabled);
        }
        
        qDebug() << "FileTransferManager: Enabled set to" << enabled;
        emit enabledChanged(enabled);
    }
}

bool FileTransferManager::isEnabled() const
{
    return m_enabled;
}

void FileTransferManager::setTargetWidget(QWidget *widget)
{
    if (m_targetWidget != widget) {
        // 清理旧控件的设置
        if (m_targetWidget) {
            m_targetWidget->setAcceptDrops(false);
        }
        
        m_targetWidget = widget;
        setupTargetWidget();
        
        qDebug() << "FileTransferManager: Target widget changed";
    }
}

QWidget* FileTransferManager::targetWidget() const
{
    return m_targetWidget;
}

bool FileTransferManager::handleDragEnterEvent(QDragEnterEvent *event)
{
    if (!m_enabled || !event) {
        return false;
    }
    
    // 检查是否包含文件URL
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        // 检查是否至少有一个本地文件
        QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile()) {
                event->acceptProposedAction();
                qDebug() << "FileTransferManager: Drag enter accepted";
                return true;
            }
        }
    }
    
    return false;
}

bool FileTransferManager::handleDropEvent(QDropEvent *event)
{
    if (!m_enabled || !event) {
        return false;
    }
    
    // 检查是否包含文件URL
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        QList<QUrl> urls = event->mimeData()->urls();
        QStringList localFiles = extractLocalFiles(urls);
        
        if (!localFiles.isEmpty()) {
            // 获取放置位置
            QPoint dropPosition = event->position().toPoint();
            
            qDebug() << "FileTransferManager: Files dropped at" << dropPosition 
                     << "Files:" << localFiles;
            
            emit filesDropped(localFiles, dropPosition.x(), dropPosition.y());
            event->acceptProposedAction();
            return true;
        }
    }
    
    return false;
}

QStringList FileTransferManager::extractLocalFiles(const QList<QUrl> &urls) const
{
    QStringList localFiles;
    
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            QString filePath = url.toLocalFile();
            QFileInfo fileInfo(filePath);
            
            // 验证文件是否存在
            if (fileInfo.exists()) {
                localFiles << filePath;
                qDebug() << "FileTransferManager: Valid local file:" << filePath;
            } else {
                qWarning() << "FileTransferManager: File does not exist:" << filePath;
            }
        } else {
            qDebug() << "FileTransferManager: Non-local URL ignored:" << url.toString();
        }
    }
    
    return localFiles;
}