#include "RemoteDesktopSession.h"
#include "ProtocolSession.h"
#include "DecodePipeline.h"
#include "../network/ConnectionManager.h"
#include "../window/ClientRemoteWindow.h"
#ifndef QT_NO_OPENGL
#include "../window/GLTextureViewport.h"
#include "../decode/GpuDecodeTarget.h"
#endif
#include "../window/InputForwarder.h"
#include "../window/CursorManager.h"
#include "../../common/clipboard/ClipboardManager.h"
#include "../../common/core/logging/LoggingCategories.h"

#include <QtCore/QThread>
#include <QtWidgets/QApplication>

RemoteDesktopSession::RemoteDesktopSession(const QString& host, int port,
                                           const QString& connectionId,
                                           QObject* parent)
    : QObject(parent)
    , m_connectionId(connectionId)
    , m_host(host)
    , m_port(port) {

    // 1. 创建网络线程
    m_networkThread = new QThread(this);
    m_networkThread->setObjectName(QString("Network-%1").arg(m_connectionId));

    // 2. 创建解码管线（Main 线程，稍后 moveToThread）
    createDecodePipeline();

    // 3. 创建网络组件（Main 线程，稍后 moveToThread）
    createNetworkComponents();

    // 4. 创建窗口（Main 线程，QWidget 约束）
    createWindow();

    // 5. 信号接线
    wireSignals();

    // 6. 将网络组件移到 Network 线程
    //    ConnectionManager 内部自建 TcpClient（父子关系），一并移入
    m_connectionManager->moveToThread(m_networkThread);
    m_protocolSession->moveToThread(m_networkThread);
    m_decodePipeline->moveToThread(m_networkThread);
}

RemoteDesktopSession::~RemoteDesktopSession() {
    if (!m_closing) {
        close();
    }
}

void RemoteDesktopSession::createNetworkComponents() {
    // ConnectionManager 构造函数内部创建 TcpClient（Qt 父子关系）
    m_connectionManager = new ConnectionManager();

    m_protocolSession = new ProtocolSession(m_connectionManager, m_decodePipeline);
    m_protocolSession->setConnectionId(m_connectionId);

    // 注意：不设置父子关系，避免 moveToThread 时报错
    // "Cannot move objects with a parent"
}

void RemoteDesktopSession::createDecodePipeline() {
    m_decodePipeline = new DecodePipeline(m_connectionId);
}

void RemoteDesktopSession::createWindow() {
    m_window = new ClientRemoteWindow(m_protocolSession, nullptr);

    // GL 挂载
#ifndef QT_NO_OPENGL
    GLTextureViewport* gl = m_window->glViewport();
    if (gl) {
        gl->attachFrameBuffer(m_decodePipeline->frameBuffer());

        // GL 上下文注入到管线
        m_decodePipeline->setGLViewport(gl);
        if (gl->decodeTarget()) {
            m_decodePipeline->setDecodeTarget(gl->decodeTarget());
        }

        // 上下文就绪信号
        connect(gl, &GLTextureViewport::glContextReady, m_decodePipeline,
            [pipeline = m_decodePipeline, gl](QOpenGLContext* ctx) {
                pipeline->setGLContext(ctx);
                if (gl->decodeTarget()) {
                    pipeline->setDecodeTarget(gl->decodeTarget());
                }
            }, Qt::QueuedConnection);

        // 信号竞态保护：若已就绪直接设置
        if (gl->context() && gl->context()->isValid()) {
            m_decodePipeline->setGLContext(gl->context());
            if (gl->decodeTarget()) {
                m_decodePipeline->setDecodeTarget(gl->decodeTarget());
            }
        }
    }
#endif

    // InputForwarder → ProtocolSession
    InputForwarder* inputForwarder = m_window->findChild<InputForwarder*>();
    if (inputForwarder) {
        inputForwarder->setProtocolSession(m_protocolSession);
    }

    m_window->show();
    m_window->raise();
    m_window->activateWindow();
    QApplication::processEvents();
}

void RemoteDesktopSession::wireSignals() {
    // ── 认证成功钩子 / 错误转发 ──
    connect(m_connectionManager, &ConnectionManager::connectionStateChanged,
        this, [this](ConnectionManager::ConnectionState state) {
            if (state == ConnectionManager::ConnectionState::Authenticated) {
                // 使用 QueuedConnection 将 startSession() 派发到 ProtocolSession 所在的 Network 线程
                QMetaObject::invokeMethod(m_protocolSession, "startSession", Qt::QueuedConnection);
            } else if (state == ConnectionManager::ConnectionState::Error) {
                emit errorOccurred(RdError(ErrorCode::NetworkConnectionFailed,
                    QStringLiteral("连接 %1:%2 失败").arg(m_host).arg(m_port),
                    "RemoteDesktopSession"));
            }
        });

    // ── 转发协议层错误 ──
    connect(m_protocolSession, &ProtocolSession::sessionError,
        this, &RemoteDesktopSession::errorOccurred);

    // ── 状态转发到 UI ──
    connect(m_protocolSession, &ProtocolSession::connectionStateChanged,
        m_window, &ClientRemoteWindow::setConnectionState);

    // ── 光标 ──
    CursorManager* cursorMgr = m_window->cursorManager();
    if (cursorMgr) {
        connect(m_protocolSession, &ProtocolSession::remoteCursorTypeUpdated,
            cursorMgr, &CursorManager::setRemoteCursorType);
    }

    // ── 剪贴板 ──
    ClipboardManager* clipboardMgr = m_window->findChild<ClipboardManager*>();
    if (clipboardMgr) {
        connect(clipboardMgr, &ClipboardManager::clipboardTextChanged,
            m_protocolSession, &ProtocolSession::sendClipboardText);
        connect(clipboardMgr, &ClipboardManager::clipboardImageChanged,
            m_protocolSession, &ProtocolSession::sendClipboardImage);
        connect(m_protocolSession, &ProtocolSession::clipboardTextReceived,
            clipboardMgr, &ClipboardManager::setText);
        connect(m_protocolSession, &ProtocolSession::clipboardImageReceived,
            clipboardMgr, &ClipboardManager::setImageFromPng);
    }

    // ── 窗口关闭 ──
    connect(m_window, &ClientRemoteWindow::windowClosed,
        this, &RemoteDesktopSession::close);
}

void RemoteDesktopSession::start() {
    qCInfo(lcSession) << "RemoteDesktopSession::start() — connecting to" << m_host << ":" << m_port << "[" << m_connectionId << "]";
    m_networkThread->start();
    QMetaObject::invokeMethod(m_connectionManager,
        "connectToHost", Qt::QueuedConnection,
        Q_ARG(QString, m_host), Q_ARG(int, m_port));
}

void RemoteDesktopSession::close() {
    if (m_closing) return;
    m_closing = true;

    qCInfo(lcSession) << "RemoteDesktopSession::close() — starting for" << m_connectionId;

    // 1. 停止解码管线（通过 BlockingQueuedConnection 在 Network 线程执行）
    if (m_decodePipeline) {
        QMetaObject::invokeMethod(m_decodePipeline, "stop", Qt::BlockingQueuedConnection);
    }

    // 2. 断开连接（通过 BlockingQueuedConnection 在 Network 线程执行）
    if (m_protocolSession) {
        QMetaObject::invokeMethod(m_protocolSession, "disconnectFromHost", Qt::BlockingQueuedConnection);
    }

    // 3. 停止 Network 线程
    if (m_networkThread && m_networkThread->isRunning()) {
        m_networkThread->quit();
        if (!m_networkThread->wait(3000)) {
            qCWarning(lcSession) << "Network thread quit timeout";
            m_networkThread->requestInterruption();
            m_networkThread->quit();
            m_networkThread->wait(1000);
        }
    }

    // 4. 删除网络层对象（线程已停止，安全删除）
    //    注意：ConnectionManager 无父对象，需要单独 delete
    delete m_connectionManager;
    m_connectionManager = nullptr;
    delete m_protocolSession;
    m_protocolSession = nullptr;
    delete m_decodePipeline;
    m_decodePipeline = nullptr;

    // 5. 关闭窗口
    if (m_window && !m_window->isClosing()) {
        m_window->close();
    }

    qCInfo(lcSession) << "RemoteDesktopSession::close() — completed for" << m_connectionId;
    emit finished(m_connectionId);
}
