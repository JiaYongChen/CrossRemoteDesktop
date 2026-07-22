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
            if (mimeData->hasImage()) {
                QImage image = qvariant_cast<QImage>(mimeData->imageData());
                if (!image.isNull()) {
                    QByteArray imageData;
                    QBuffer buffer(&imageData);
                    buffer.open(QIODevice::WriteOnly);
                    image.save(&buffer, "PNG");
                    m_lastImageData = imageData;
                }
            }
            if (mimeData->hasText()) {
                m_lastText = mimeData->text();
            }
        }
        qCInfo(lcClipboard) << "剪贴板监听已启用";
    } else {
        m_lastText.clear();
        m_lastImageData.clear();
        m_lastReceivedText.clear();
        m_lastReceivedImage = QImage();
        qCInfo(lcClipboard) << "剪贴板监听已禁用";
    }
}

void ClipboardManager::setText(const QString& text) {
    if (!m_enabled) return;
    if (text == m_lastText) return;

    m_lastText = text;
    m_lastReceivedText.clear();
    m_clipboard->setText(text);

    qCDebug(lcClipboard) << "设置剪贴板文本，长度:" << text.length();
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
    m_lastReceivedImage = QImage();
    m_clipboard->setImage(image);

    qCDebug(lcClipboard) << "设置剪贴板图片，尺寸:" << image.size();
}

void ClipboardManager::setImageFromPng(const QByteArray& pngData) {
    if (!m_enabled) return;
    if (pngData.isEmpty()) return;

    if (pngData.size() > kMaxClipboardImageSize) {
        qCWarning(lcClipboard) << "剪贴板图片超过 10MB 上限，已跳过，大小:" << pngData.size();
        return;
    }

    QImage image;
    if (!image.loadFromData(pngData, "PNG")) {
        qCWarning(lcClipboard) << "PNG 图片解码失败";
        return;
    }

    setImage(image);
}

void ClipboardManager::applyRemoteText(const QString& text) {
    if (!m_enabled) return;

    m_lastReceivedText = text;
    m_lastText = text;
    m_lastImageData.clear();
    m_lastReceivedImage = QImage();
    m_clipboard->setText(text);

    qCDebug(lcClipboard) << "应用远端文本，长度:" << text.length();
}

void ClipboardManager::applyRemoteImage(const QByteArray& pngData) {
    if (!m_enabled) return;
    if (pngData.isEmpty()) return;

    if (pngData.size() > kMaxClipboardImageSize) {
        qCWarning(lcClipboard) << "远端剪贴板图片超过 10MB 上限，已跳过，大小:" << pngData.size();
        return;
    }

    QImage image;
    if (!image.loadFromData(pngData, "PNG")) {
        qCWarning(lcClipboard) << "远端 PNG 图片解码失败";
        return;
    }

    // 基于像素内容设置去重标记（避免 PNG 编码字节差异导致去重失效）
    m_lastReceivedImage = image;
    m_lastImageData.clear();
    m_lastText.clear();
    m_lastReceivedText.clear();
    m_clipboard->setImage(image);

    qCDebug(lcClipboard) << "应用远端图片，尺寸:" << image.size();
}

void ClipboardManager::onClipboardChanged(QClipboard::Mode mode) {
    if (mode != QClipboard::Clipboard) return;
    if (!m_enabled) return;

    const QMimeData* mimeData = m_clipboard->mimeData();
    if (!mimeData) return;

    // 图片分支（独立处理，不再用 else if 以免跳过文本）
    if (mimeData->hasImage()) {
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull()) {

            QByteArray imageData;
            QBuffer buffer(&imageData);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");

            if (imageData.size() > kMaxClipboardImageSize) {
                qCWarning(lcClipboard) << "剪贴板图片超过 10MB 上限，已跳过，大小:" << imageData.size();
                return;
            }

            // 像素级内容去重：比对 QImage 而非 PNG 字节
            if (!m_lastReceivedImage.isNull() && image == m_lastReceivedImage) {
                m_lastReceivedImage = QImage();
                return;
            }

            if (imageData != m_lastImageData) {
                m_lastImageData = imageData;
                m_lastText.clear();
                m_lastReceivedText.clear();
                m_lastReceivedImage = QImage();

                qCDebug(lcClipboard) << "剪贴板图片变化，尺寸:" << image.size();
                emit clipboardImageChanged(imageData, image.width(), image.height());
            }
        }
    }

    // 文本分支（独立 if，支持图文共存时分别处理）
    if (mimeData->hasText()) {
        QString text = mimeData->text();

        // 内容去重：与上次远端内容匹配则跳过回环
        if (!m_lastReceivedText.isEmpty() && text == m_lastReceivedText) {
            m_lastReceivedText.clear();
            return;
        }

        if (text != m_lastText) {
            m_lastText = text;
            m_lastImageData.clear();
            m_lastReceivedText.clear();
            m_lastReceivedImage = QImage();

            qCDebug(lcClipboard) << "剪贴板文本变化，长度:" << text.length();
            emit clipboardTextChanged(text);
        }
    }
}
