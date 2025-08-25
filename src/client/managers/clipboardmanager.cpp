#include "clipboardmanager.h"
#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>
#include <QtCore/QDebug>

ClipboardManager::ClipboardManager(QObject *parent)
    : QObject(parent)
    , m_syncEnabled(true)
    , m_clipboard(QApplication::clipboard())
    , m_lastClipboardText()
{
    setupConnections();
    
    // 初始化当前剪贴板内容
    if (m_clipboard) {
        m_lastClipboardText = m_clipboard->text();
    }
}

ClipboardManager::~ClipboardManager()
{
    // Qt对象会自动清理，无需手动删除
}

void ClipboardManager::setupConnections()
{
    if (m_clipboard) {
        // 连接系统剪贴板变化信号
        connect(m_clipboard, &QClipboard::dataChanged, 
                this, &ClipboardManager::onSystemClipboardChanged);
    }
}

void ClipboardManager::setSyncEnabled(bool enabled)
{
    if (m_syncEnabled != enabled) {
        m_syncEnabled = enabled;
        qDebug() << "ClipboardManager: Sync enabled set to" << enabled;
    }
}

bool ClipboardManager::isSyncEnabled() const
{
    return m_syncEnabled;
}

QString ClipboardManager::currentText() const
{
    if (m_clipboard) {
        return m_clipboard->text();
    }
    return QString();
}

void ClipboardManager::setText(const QString &text)
{
    if (m_clipboard && text != m_lastClipboardText) {
        // 临时禁用同步，避免触发自己的变化信号
        bool wasEnabled = m_syncEnabled;
        m_syncEnabled = false;
        
        m_clipboard->setText(text);
        m_lastClipboardText = text;
        
        // 恢复同步状态
        m_syncEnabled = wasEnabled;
        
        qDebug() << "ClipboardManager: Text set to clipboard:" << text.left(50) + (text.length() > 50 ? "..." : "");
    }
}

void ClipboardManager::onSystemClipboardChanged()
{
    if (!m_syncEnabled || !m_clipboard) {
        return;
    }
    
    QString currentText = m_clipboard->text();
    
    // 只有当文本真正发生变化时才发出信号
    if (currentText != m_lastClipboardText) {
        m_lastClipboardText = currentText;
        
        qDebug() << "ClipboardManager: Clipboard changed, new text:" 
                 << currentText.left(50) + (currentText.length() > 50 ? "..." : "");
        
        emit clipboardChanged(currentText);
    }
}