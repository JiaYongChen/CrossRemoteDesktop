#include "ClipboardManager.h"

#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>

#include "common/config/ProtocolConstants.h"
#include "common/logging/LoggingCategories.h"

namespace {
    // 剪贴板图片原始数据上限 = 网络载荷上限扣除 IMAGE 消息头(type+w+h+dataSize=13字节)，
    // 保证通过本地校验的图片其网络载荷也不超 ProtocolConstants::MaxClipboardPayloadSize
    constexpr int kMaxClipboardImageSize = static_cast<int>(ProtocolConstants::MaxClipboardPayloadSize) - 13;
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
        m_lastFileHash.clear();
        m_lastFileList = ClipboardFileList();
        m_lastFilePaths.clear();
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

void ClipboardManager::resync() {
    if (!m_enabled) return;

    // 使用独立 if（非 else-if）：文本与文件列表可共存，互不抑制
    if (!m_lastText.isEmpty()) {
        emit clipboardTextChanged(m_lastText);
    }
    if (!m_lastImageData.isEmpty()) {
        QImage image;
        if (image.loadFromData(m_lastImageData, "PNG")) {
            emit clipboardImageChanged(m_lastImageData,
                                       static_cast<quint32>(image.width()),
                                       static_cast<quint32>(image.height()));
        }
    }
    if (!m_lastFileList.files.isEmpty()) {
        emit clipboardFilesChanged(m_lastFileList);
    }
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

    // 规范化为统一格式存储，避免跨平台格式漂移导致像素比对失效
    m_lastReceivedImage = image.convertToFormat(QImage::Format_ARGB32);
    m_lastText.clear();
    m_lastReceivedText.clear();
    m_clipboard->setImage(image);

    qCDebug(lcClipboard) << "应用远端图片，尺寸:" << image.size();
}

void ClipboardManager::applyRemoteFiles(const ClipboardFileList& fileList) {
    if (!m_enabled) return;

    // 清空去重哈希防回环：远端列表不写入系统剪贴板（无 dataChanged 回环），
    // 但需保证后续本地复制同类文件会重新触发同步
    m_lastFileHash.clear();
    // 存储元数据作为 resync 基线（与文本/图片一致）
    m_lastFileList = fileList;
    // 远端列表在本机无对应文件，路径映射必须清空（服务端响应请求时会因查不到路径而拒绝）
    m_lastFilePaths.clear();
    m_lastText.clear();
    m_lastImageData.clear();
    m_lastReceivedText.clear();
    m_lastReceivedImage = QImage();

    qCDebug(lcClipboard) << "应用远端文件列表，条目数:" << fileList.files.size();
}

ClipboardFileList ClipboardManager::extractFiles(const QList<QUrl>& urls,
                                                   QVector<QString>* outPaths) {
    ClipboardFileList list;
    if (outPaths) outPaths->clear();
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) continue;

        const QFileInfo info(url.toLocalFile());
        if (!info.exists()) continue;

        ClipboardFileInfo fileInfo;
        fileInfo.fileName = info.fileName();
        fileInfo.fileSize = info.isDir() ? 0 : info.size();
        fileInfo.modifyTimeMs = info.lastModified().toMSecsSinceEpoch();
        fileInfo.isDirectory = info.isDir();
        list.files.append(fileInfo);
        if (outPaths) outPaths->append(info.absoluteFilePath());

        if (list.files.size() >= static_cast<qsizetype>(ProtocolConstants::MaxFileListCount)) break;
    }
    return list;
}

QByteArray ClipboardManager::computeFileListHash(const ClipboardFileList& fileList) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const ClipboardFileInfo& info : fileList.files) {
        hash.addData(info.fileName.toUtf8());
        hash.addData("\x1f", 1);  // ASCII 单元分隔符，防字段边界歧义
        hash.addData(QByteArray::number(info.fileSize));
        hash.addData("\x1f", 1);
        hash.addData(QByteArray::number(info.modifyTimeMs));
        hash.addData("\x1e", 1);  // ASCII 记录分隔符，防条目边界歧义
    }
    return hash.result();
}

void ClipboardManager::onClipboardChanged(QClipboard::Mode mode) {
    if (mode != QClipboard::Clipboard) return;
    if (!m_enabled) return;

    const QMimeData* mimeData = m_clipboard->mimeData();
    if (!mimeData) return;

    // 图片分支（独立处理，用 goto end_image 替代 return 以免跳过文本分支）
    if (mimeData->hasImage()) {
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull()) {

            // 像素级去重优先（比 PNG 编码便宜，避免回环时浪费 CPU）
            if (!m_lastReceivedImage.isNull()) {
                const QImage canonical = image.convertToFormat(QImage::Format_ARGB32);
                if (canonical == m_lastReceivedImage) {
                    m_lastReceivedImage = QImage();
                    m_lastReceivedText.clear();
                    goto end_image;
                }
            }

            // 仅确认非回环后才做 PNG 编码
            QByteArray imageData;
            QBuffer buffer(&imageData);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");

            if (imageData.size() > kMaxClipboardImageSize) {
                qCWarning(lcClipboard) << "剪贴板图片超过 10MB 上限，已跳过，大小:" << imageData.size();
                goto end_image;
            }

            if (imageData != m_lastImageData) {
                m_lastImageData = imageData;
                m_lastText.clear();
                m_lastReceivedText.clear();
                m_lastReceivedImage = QImage();
                m_lastFileHash.clear();

                qCDebug(lcClipboard) << "剪贴板图片变化，尺寸:" << image.size();
                emit clipboardImageChanged(imageData, image.width(), image.height());
            }
        }
        end_image: ;
    }

    // 文本分支（独立 if，支持图文共存时分别处理）
    if (mimeData->hasText()) {
        QString text = mimeData->text();

        // 内容去重：与上次远端内容匹配则跳过回环
        if (!m_lastReceivedText.isEmpty() && text == m_lastReceivedText) {
            m_lastReceivedText.clear();
            goto end_text;
        }

        if (text != m_lastText) {
            m_lastText = text;
            m_lastImageData.clear();
            m_lastReceivedText.clear();
            m_lastReceivedImage = QImage();
            m_lastFileHash.clear();

            qCDebug(lcClipboard) << "剪贴板文本变化，长度:" << text.length();
            emit clipboardTextChanged(text);
        }
        end_text: ;
    }

    // 文件列表分支（独立 if，最后处理；与文本/图片共用哈希去重基线）
    if (mimeData->hasUrls()) {
        ClipboardFileList fileList = extractFiles(mimeData->urls(), &m_lastFilePaths);
        if (!fileList.files.isEmpty()) {
            const QByteArray hash = computeFileListHash(fileList);
            if (hash != m_lastFileHash) {
                m_lastFileHash = hash;
                m_lastFileList = fileList;
                m_lastText.clear();
                m_lastImageData.clear();
                m_lastReceivedText.clear();
                m_lastReceivedImage = QImage();

                qCDebug(lcClipboard) << "剪贴板文件列表变化，条目数:" << fileList.files.size();
                emit clipboardFilesChanged(fileList);
            }
        }
    }
}
