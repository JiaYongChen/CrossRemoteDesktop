#include "RemoteDesktopSession.h"
#include "ProtocolSession.h"
#include "DecodePipeline.h"
#include "../network/ConnectionManager.h"
#include "../windows/ClientRemoteWindow.h"
#ifndef QT_NO_OPENGL
#include "../windows/GLTextureViewport.h"
#include "../decode/GpuDecodeTarget.h"
#endif
#include "../windows/InputForwarder.h"
#include "../windows/CursorManager.h"
#include "../clipboard/ClipboardManager.h"
#include "../../common/logging/LoggingCategories.h"

#include <QtWidgets/QApplication>

RemoteDesktopSession::RemoteDesktopSession(const ConnectionParams& params,
                                           const QString& connectionId,
                                           QObject* parent)
    : QObject(parent)
    , m_connectionId(connectionId)
    , m_params(params) {

    // 所有网络对象在 Main 线程创建并保持——QSslSocket 的平台级回调绑定 Main 线程，
    // Qt 异步 I/O 不阻塞 GUI；只有解码在独立 DecodeThread 进行。

    createDecodePipeline();
    createNetworkComponents();
    createWindow();
    wireSignals();

    // 不需要 Network 线程：TcpClient/ConnectionManager/ProtocolSession/DecodePipeline 全部在 Main 线程
}

RemoteDesktopSession::~RemoteDesktopSession() {
    if (!m_closing) {
        close();
    }
}

void RemoteDesktopSession::createNetworkComponents() {
    // ConnectionManager 构造函数内部创建 TcpClient（Qt 父子关系）
    m_connectionManager = new ConnectionManager();

    // ── 预设认证凭证（在 connectToHost 前，handleHandshakeResponse 会自动使用）──
    m_connectionManager->setCredentials(m_params.username, m_params.password);

    // ── 网络参数配置 ──
    m_connectionManager->setConnectionTimeout(m_params.connectionTimeout);
    m_connectionManager->setAutoReconnect(m_params.autoReconnect);
    m_connectionManager->setReconnectInterval(m_params.reconnectInterval);

    // ── 显示参数（握手携带到服务端）──
    m_connectionManager->setColorDepth(m_params.colorDepth);
    m_connectionManager->setImageQuality(m_params.imageQuality);

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

    // ── CursorManager 注入到 InputForwarder 和 GLTextureViewport ──
    CursorManager* cursorMgr = m_window->cursorManager();
    if (cursorMgr) {
        // 设置坐标映射所需的服务端屏幕尺寸
        if (m_protocolSession) {
            cursorMgr->setRemoteScreenSize(m_protocolSession->remoteScreenSize());
        }
        if (inputForwarder) {
            inputForwarder->setCursorManager(cursorMgr);
        }
    #ifndef QT_NO_OPENGL
        if (gl) {
            gl->setCursorManager(cursorMgr);
        }
    #endif
    }

    // ── 分发 ConnectionParams 配置到窗口组件 ──

    // 主机名 → 窗口标题
    if (!m_params.hostname.isEmpty()) {
        m_window->updateWindowTitle(m_params.hostname);
    }

    // 全屏模式
    m_window->setFullScreen(m_params.fullScreen);

    // 窗口尺寸（仅非全屏时生效）
    if (!m_params.fullScreen) {
        m_window->resize(m_params.windowWidth, m_params.windowHeight);
    }

    // 仅查看模式 → 禁用输入转发
    m_window->setInputEnabled(!m_params.viewOnly);

    // 仅查看模式 → 叠加层角标 + 标题后缀
    m_window->setViewOnly(m_params.viewOnly);

    // 剪贴板共享标志（供 toggleViewOnly 恢复时使用）
    m_window->setShareClipboard(m_params.shareClipboard);

    // 剪贴板同步
    ClipboardManager* clipboardMgr = m_window->findChild<ClipboardManager*>();
    if (clipboardMgr) {
        clipboardMgr->setEnabled(m_params.shareClipboard && !m_params.viewOnly);
    }

    // 远程光标显隐
    if (cursorMgr) {
        cursorMgr->setCursorEnabled(m_params.showCursor);
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
                // 握手已完成，立即传播远程屏幕尺寸给 CursorManager
                // （比首帧 SCREEN_DATA 更早，消除首帧光标 (0,0) 问题）
                QSize remoteSz = m_connectionManager->remoteScreenSize();
                if (remoteSz.isValid()) {
                    m_protocolSession->setRemoteScreenSize(remoteSz);
                }
                // 使用 QueuedConnection 将 startSession() 派发到 ProtocolSession 所在的 Network 线程
                m_protocolSession->startSession();
            } else if (state == ConnectionManager::ConnectionState::Error) {
                emit errorOccurred(RdError(ErrorCode::NetworkConnectionFailed,
                    QStringLiteral("连接 %1:%2 失败").arg(m_params.host).arg(m_params.port),
                    "RemoteDesktopSession"));
            }
        });

    // ── 转发协议层错误 ──
    connect(m_protocolSession, &ProtocolSession::sessionError,
        this, &RemoteDesktopSession::errorOccurred);

    // ── 状态转发到 UI ──
    connect(m_protocolSession, &ProtocolSession::connectionStateChanged,
        m_window, &ClientRemoteWindow::setConnectionState);

    // ── 光标（cursorChanged→update 确保屏幕静止无帧时光标仍可渲染）
    CursorManager* cursorMgr = m_window->cursorManager();
    if (cursorMgr) {
        connect(m_protocolSession, &ProtocolSession::cursorUpdated,
                cursorMgr, &CursorManager::updateCursor);
        connect(m_protocolSession, &ProtocolSession::remoteScreenSizeChanged,
                cursorMgr, &CursorManager::setRemoteScreenSize);
    #ifndef QT_NO_OPENGL
        GLTextureViewport* glv = m_window->glViewport();
        if (glv) {
            connect(cursorMgr, &CursorManager::cursorChanged,
                    glv, qOverload<>(&QWidget::update));
        }
    #endif
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
    qCInfo(lcClientSession) << "RemoteDesktopSession::start() — connecting to"
                      << m_params.host << ":" << m_params.port
                      << "[" << m_connectionId << "]";
    // 网络对象在 Main 线程，直接调用即可
    m_connectionManager->connectToHost(m_params.host, m_params.port);
}

void RemoteDesktopSession::close() {
    if (m_closing) return;
    m_closing = true;

    qCInfo(lcClientSession) << "RemoteDesktopSession::close() — starting for" << m_connectionId;

    if (m_window) {
        disconnect(m_window, &ClientRemoteWindow::windowClosed, this, &RemoteDesktopSession::close);
    }

    // 1. 停止解码管线（内部 quit+wait DecodeThread）
    if (m_decodePipeline) {
        m_decodePipeline->stop();
    }

    // 2. 断开网络连接
    if (m_protocolSession) {
        m_protocolSession->disconnectFromHost();
    }

    // 3. 关闭窗口（由 closeEvent 触发时不重复调用——窗口已在关闭流程中）
    if (m_window && !m_window->isClosing()) {
        m_window->close();
    }

    qCInfo(lcClientSession) << "RemoteDesktopSession::close() — completed for" << m_connectionId;
    emit finished(m_connectionId);
}
