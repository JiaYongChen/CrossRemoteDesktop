#include "ClipboardManager.h"
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtGui/QImage>
#include <QtCore/QMimeData>
#include <QtCore/QBuffer>
#include "../logging/LoggingCategories.h"

namespace {
    constexpr int kMaxClipboardImageSize = 10 * 1024 * 1024;  // 10MB
}

ClipboardManager::ClipboardManager(QObject* parent)
    : QObject(parent)
    , m_clipboard(QGuiApplication::clipboard())
    , m_enabled(false)
{
    connect(m_clipboard, &QClipboard::dataChanged,
            this, [this]() { onClipboardChanged(QClipboard::Clipboard); });
}

ClipboardManager::~ClipboardManager() = default;

void ClipboardManager::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;

    m_enabled = enabled;

    if (m_enabled) {
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
        qCInfo(lcClient) << "剪贴板监听已启用";
    } else {
        m_lastText.clear();
        m_lastImageData.clear();
        qCInfo(lcClient) << "剪贴板监听已禁用";
    }
}

void ClipboardManager::setText(const QString& text) {
    if (!m_enabled) return;
    if (text == m_lastText) return;

    m_lastText = text;
    m_clipboard->setText(text);

    qCDebug(lcClient) << "设置剪贴板文本，长度:" << text.length();
}

void ClipboardManager::setImage(const QImage& image) {
    if (!m_enabled) return;
    if (image.isNull()) return;

    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");

    if (imageData == m_lastImageData) return;

    m_lastImageData = imageData;
    m_clipboard->setImage(image);

    qCDebug(lcClient) << "设置剪贴板图片，尺寸:" << image.size();
}

void ClipboardManager::setImageFromPng(const QByteArray& pngData) {
    if (!m_enabled) return;
    if (pngData.isEmpty()) return;

    if (pngData.size() > kMaxClipboardImageSize) {
        qCWarning(lcClient) << "剪贴板图片超过 10MB 上限，已跳过，大小:" << pngData.size();
        return;
    }

    QImage image;
    if (!image.loadFromData(pngData, "PNG")) {
        qCWarning(lcClient) << "PNG 图片解码失败";
        return;
    }

    setImage(image);
}

void ClipboardManager::applyRemoteText(const QString& text) {
    if (!m_enabled) return;

    m_lastReceivedText = text;
    m_lastText = text;
    m_clipboard->setText(text);

    qCDebug(lcClient) << "应用远端文本，长度:" << text.length();
}

void ClipboardManager::applyRemoteImage(const QByteArray& pngData) {
    if (!m_enabled) return;
    if (pngData.isEmpty()) return;

    if (pngData.size() > kMaxClipboardImageSize) {
        qCWarning(lcClient) << "远端剪贴板图片超过 10MB 上限，已跳过，大小:" << pngData.size();
        return;
    }

    QImage image;
    if (!image.loadFromData(pngData, "PNG")) {
        qCWarning(lcClient) << "远端 PNG 图片解码失败";
        return;
    }

    m_lastReceivedImageData = pngData;
    m_lastImageData = pngData;
    m_clipboard->setImage(image);

    qCDebug(lcClient) << "应用远端图片，尺寸:" << image.size();
}

void ClipboardManager::onClipboardChanged(QClipboard::Mode mode) {
    if (mode != QClipboard::Clipboard) return;
    if (!m_enabled) return;

    const QMimeData* mimeData = m_clipboard->mimeData();
    if (!mimeData) return;

    if (mimeData->hasImage()) {
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (image.isNull()) return;

        QByteArray imageData;
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");

        if (imageData.size() > kMaxClipboardImageSize) {
            qCWarning(lcClient) << "剪贴板图片超过 10MB 上限，已跳过，大小:" << imageData.size();
            return;
        }

        // 内容去重：与上次远端内容匹配则跳过回环
        if (!m_lastReceivedImageData.isEmpty() && imageData == m_lastReceivedImageData) {
            m_lastReceivedImageData.clear();
            return;
        }

        if (imageData != m_lastImageData) {
            m_lastImageData = imageData;
            m_lastText.clear();

            qCDebug(lcClient) << "剪贴板图片变化，尺寸:" << image.size();
            emit clipboardImageChanged(imageData, image.width(), image.height());
        }
    } else if (mimeData->hasText()) {
        QString text = mimeData->text();

        // 内容去重：与上次远端内容匹配则跳过回环
        if (!m_lastReceivedText.isEmpty() && text == m_lastReceivedText) {
            m_lastReceivedText.clear();
            return;
        }

        if (text != m_lastText) {
            m_lastText = text;
            m_lastImageData.clear();

            qCDebug(lcClient) << "剪贴板文本变化，长度:" << text.length();
            emit clipboardTextChanged(text);
        }
    }
}
