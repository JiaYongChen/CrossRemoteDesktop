#pragma once

#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtCore/QString>

#include "common/error/RdError.h"
#include "common/network/Protocol.h"
#include "client/network/ConnectionManager.h"

class DecodePipeline;

/**
 * @brief 协议会话 — 协议消息编解码 + 路由 + 会话生命周期
 *
 * 归属 Main 线程（与 ConnectionManager / DecodePipeline 同线程）。
 * 只做协议相关的事：消息编解码 + 路由。
 * 不持有 TripleBuffer、GL 上下文、解码线程。
 */
class ProtocolSession : public QObject {
    Q_OBJECT
public:
    explicit ProtocolSession(ConnectionManager* connectionManager,
                             DecodePipeline* pipeline,
                             QObject* parent = nullptr);

    // ── 会话生命周期 ──
    void startSession();
    bool isActive() const;

    // ── 连接状态（代理到 ConnectionManager）──
    bool isConnected() const;

    // ── 远程屏幕尺寸（本地状态，由握手/SCALED 帧更新）──
    QSize remoteScreenSize() const { return m_remoteScreenSize; }
    void setRemoteScreenSize(QSize sz) {
        if (sz.isValid() && sz != m_remoteScreenSize) {
            m_remoteScreenSize = sz;
            emit remoteScreenSizeChanged(m_remoteScreenSize);
        }
    }

    // ── 输入事件序列化（跨线程 slots，从 InputForwarder 调用）──
public slots:
    void sendMouseEvent(int x, int y, int eventType);
    void sendKeyboardEvent(int key, int modifiers, bool pressed, const QString& text);
    void sendWheelEvent(int x, int y, int delta, int orientation);

    // ── 剪贴板序列化 ──
    void sendClipboardText(const QString& text);
    void sendClipboardImage(const QByteArray& imageData, quint32 width, quint32 height);

    // ── 连接控制 ──
    void disconnectFromHost();

signals:
    // → CursorManager (Main 线程，直接连接)
    void cursorUpdated(const CursorMessage& msg);
    void remoteScreenSizeChanged(QSize sz);

    // → ClipboardManager (Main 线程，QueuedConnection)
    void clipboardTextReceived(const QString& text);
    void clipboardImageReceived(const QByteArray& imageData);

    // → UI (Main 线程，QueuedConnection)
    void connectionStateChanged(ConnectionManager::ConnectionState state);
    void sessionError(const RdError& error);

private slots:
    void onMessageReceived(MessageType type, const QByteArray& data);

private:
    void setupConnections();
    void handleScreenData(const QByteArray& data);
    void handleCursorPosition(const QByteArray& data);
    void handleClipboardData(const QByteArray& data);

    ConnectionManager* m_connectionManager;
    DecodePipeline*   m_pipeline;

    // 远程桌面数据
    QSize m_remoteScreenSize;
};
