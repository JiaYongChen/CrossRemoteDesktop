#include "ClipboardManager.h"
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtGui/QImage>
#include <QtCore/QMimeData>
#include <QtCore/QBuffer>
#include <QtCore/QDebug>

ClipboardManager::ClipboardManager(QObject* parent)
    : QObject(parent)
    , m_clipboard(QGuiApplication::clipboard())
    , m_enabled(false)
    , m_ignoreNextChange(false) {
    
    // 连接剪贴板变化信号
    connect(m_clipboard, &QClipboard::dataChanged,
            this, [this]() { onClipboardChanged(QClipboard::Clipboard); });
}

ClipboardManager::~ClipboardManager() {
}

void ClipboardManager::setEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }
    
    m_enabled = enabled;
    
    if (m_enabled) {
        // 启用时，读取当前剪贴板内容作为初始状态
        const QMimeData* mimeData = m_clipboard->mimeData();
        if (mimeData) {
            if (mimeData->hasText()) {
                m_lastText = mimeData->text();
            } else if (mimeData->hasImage()) {
                QImage image = qvariant_cast<QImage>(mimeData->imageData());
                if (!image.isNull()) {
                    QByteArray imageData;
                    QBuffer buffer(&imageData);
                    buffer.open(QIODevice::WriteOnly);
                    image.save(&buffer, "PNG");
                    m_lastImageData = imageData;
                }
            }
        }
        qDebug() << "ClipboardManager: Enabled";
    } else {
        // 禁用时清空状态
        m_lastText.clear();
        m_lastImageData.clear();
        qDebug() << "ClipboardManager: Disabled";
    }
}

void ClipboardManager::setText(const QString& text) {
    if (text == m_lastText) {
        return;
    }
    
    // 设置标志，避免触发自己的变化事件
    m_ignoreNextChange = true;
    m_lastText = text;
    
    m_clipboard->setText(text);
    
    qDebug() << "ClipboardManager: Set text to clipboard, length:" << text.length();
}

void ClipboardManager::setImage(const QImage& image) {
    if (image.isNull()) {
        return;
    }
    
    // 转换为 PNG 数据
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    
    if (imageData == m_lastImageData) {
        return;
    }
    
    // 设置标志，避免触发自己的变化事件
    m_ignoreNextChange = true;
    m_lastImageData = imageData;
    
    m_clipboard->setImage(image);
    
    qDebug() << "ClipboardManager: Set image to clipboard, size:" << image.size();
}

void ClipboardManager::setImageFromPng(const QByteArray& pngData) {
    if (pngData.isEmpty()) {
        return;
    }
    
    QImage image;
    if (!image.loadFromData(pngData, "PNG")) {
        qWarning() << "ClipboardManager: Failed to load image from PNG data";
        return;
    }
    
    setImage(image);
}

void ClipboardManager::onClipboardChanged(QClipboard::Mode mode) {
    // 只处理默认剪贴板（不处理选择缓冲区）
    if (mode != QClipboard::Clipboard) {
        return;
    }
    
    // 如果被禁用，不处理
    if (!m_enabled) {
        return;
    }
    
    // 如果是自己触发的变化，忽略
    if (m_ignoreNextChange) {
        m_ignoreNextChange = false;
        return;
    }
    
    const QMimeData* mimeData = m_clipboard->mimeData();
    if (!mimeData) {
        return;
    }
    
    // 优先处理图片
    if (mimeData->hasImage()) {
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull()) {
            // 转换为 PNG 格式
            QByteArray imageData;
            QBuffer buffer(&imageData);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            
            // 检查是否与上次相同
            if (imageData != m_lastImageData) {
                m_lastImageData = imageData;
                m_lastText.clear();  // 清空文本状态
                
                qDebug() << "ClipboardManager: Image changed, size:" << image.size();
                emit clipboardImageChanged(imageData, image.width(), image.height());
            }
        }
    }
    // 处理文本
    else if (mimeData->hasText()) {
        QString text = mimeData->text();
        
        // 检查是否与上次相同
        if (text != m_lastText) {
            m_lastText = text;
            m_lastImageData.clear();  // 清空图片状态
            
            qDebug() << "ClipboardManager: Text changed, length:" << text.length();
            emit clipboardTextChanged(text);
        }
    }
}
