#include "RemoteDesktopSession.h"

#include <QtWidgets/QApplication>

#include "client/decode/GpuDecodeTarget.h"
#include "client/network/ConnectionManager.h"
#include "client/session/DecodePipeline.h"
#include "client/session/ProtocolSession.h"
#include "client/window/ClientRemoteWindow.h"
#include "client/window/ConnectionLifecycle.h"
#include "client/window/CursorManager.h"
#include "client/window/GLTextureViewport.h"
#include "client/window/InputForwarder.h"
#include "common/clipboard/ClipboardManager.h"
#include "common/logging/LoggingCategories.h"

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
    m_connectionManager = new ConnectionManager(this);

    // ── 预设认证凭证（在 connectToHost 前，handleHandshakeResponse 会自动使用）──
    m_connectionManager->setCredentials(m_params.username, m_params.password);

    // ── 网络参数配置 ──
    m_connectionManager->setConnectionTimeout(m_params.connectionTimeout);
    m_connectionManager->setAutoReconnect(m_params.autoReconnect);
    m_connectionManager->setReconnectInterval(m_params.reconnectInterval);

    // ── 显示参数（握手携带到服务端）──
    m_connectionManager->setColorDepth(m_params.colorDepth);
    m_connectionManager->setImageQuality(m_params.imageQuality);

    // 所有客户端组件驻留 Main 线程（Network 线程已于架构简化中移除），
    // 设 this 为 parent 实现自动析构清理
    m_protocolSession = new ProtocolSession(m_connectionManager, m_decodePipeline, this);
}

void RemoteDesktopSession::createDecodePipeline() {
    m_decodePipeline = new DecodePipeline(m_connectionId, this);
}

void RemoteDesktopSession::createWindow() {
    m_window = new ClientRemoteWindow(m_protocolSession, nullptr);

    // GL 挂载
    GLTextureViewport* gl = m_window->glViewport();
    if (gl) {
        gl->attachFrameBuffer(m_decodePipeline->frameBuffer());

        // GL 视口注入到管线
        m_decodePipeline->setGLViewport(gl);

        // GL 资源销毁前停止管线，避免 DecodeWorker 持有的 GpuDecodeTarget
        // 裸指针在上下文重建时悬空（use-after-free）
        connect(gl, &GLTextureViewport::glResourcesAboutToBeDestroyed,
            m_decodePipeline, &DecodePipeline::stop);

        // GpuDecodeTarget 与 GL 上下文均在首次曝光后的 initializeGL 中创建，
        // 此处（show 之前）唯一有效的注入路径是 glContextReady 信号
        connect(gl, &GLTextureViewport::glContextReady, m_decodePipeline,
            [this, pipeline = m_decodePipeline, gl]() {
                pipeline->notifyGLReady();
                if (gl->decodeTarget()) {
                    pipeline->setDecodeTarget(gl->decodeTarget());
                }
                // GL 上下文重建后重启管线（首次初始化阶段 isActive 为 false，跳过）
                if (m_protocolSession->isActive() && !pipeline->isRunning()) {
                    pipeline->start();
                }
            });
    }

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
        if (gl) {
            gl->setCursorManager(cursorMgr);
        }
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

    // ── 注入缓存的用户名到 ConnectionLifecycle（用于凭据重输对话框预填）──
    ConnectionLifecycle* lifecycle = m_window->connectionLifecycle();
    if (lifecycle) {
        lifecycle->setCachedUsername(m_params.username);
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
                // 同线程直接调用启动会话（全部客户端对象均驻留 Main 线程）
                m_protocolSession->startSession();
                // 认证成功：上报实际通过验证的凭据（重试后可能与初始入参不同），
                // 供 MainWindow 回写连接历史
                emit authenticated(m_connectionId, m_connectionManager->username(),
                                   m_connectionManager->password());
            }
        });

    // ── 连接层错误转发（TcpClient 经 ConnectionManager 直传，保留中文诊断）──
    connect(m_connectionManager, &ConnectionManager::errorOccurred,
        this, &RemoteDesktopSession::errorOccurred);

    // ── 转发协议层错误 ──
    connect(m_protocolSession, &ProtocolSession::sessionError,
        this, &RemoteDesktopSession::errorOccurred);

    // ── 转发解码错误（DecodeWorker 失败流首帧上报）──
    connect(m_decodePipeline, &DecodePipeline::decodeError,
        this, &RemoteDesktopSession::errorOccurred);

    // ── 状态转发到 UI ──
    connect(m_protocolSession, &ProtocolSession::connectionStateChanged,
        m_window, &ClientRemoteWindow::setConnectionState);

    // ── 认证错误转发到 ConnectionLifecycle（用于凭据重输对话框）──
    ConnectionLifecycle* lifecycle = m_window->connectionLifecycle();
    if (lifecycle) {
        // 认证失败错误消息 → 缓存到 lifecycle（先于 setConnectionState 到达）
        connect(m_connectionManager, &ConnectionManager::errorOccurred,
                lifecycle, [lifecycle](const RdError& error) {
            switch (error.code) {
            case ErrorCode::AuthInvalidUsername:
            case ErrorCode::AuthInvalidPassword:
            case ErrorCode::AuthAccessDenied:
                lifecycle->setAuthErrorCode(error.code);
                lifecycle->setAuthErrorMessage(error.message);
                break;
            default:
                break;
            }
        });

        // 凭据重试 → 更新凭据并重连
        connect(lifecycle, &ConnectionLifecycle::retryAuthRequested,
                this, [this](const QString& username, const QString& password) {
            m_connectionManager->updateCredentials(username, password);
            m_connectionManager->retryAuthentication();
        });
    }

    // ── 光标（cursorChanged→update 确保屏幕静止无帧时光标仍可渲染）
    CursorManager* cursorMgr = m_window->cursorManager();
    if (cursorMgr) {
        connect(m_protocolSession, &ProtocolSession::cursorUpdated,
                cursorMgr, &CursorManager::updateCursor);
        connect(m_protocolSession, &ProtocolSession::remoteScreenSizeChanged,
                cursorMgr, &CursorManager::setRemoteScreenSize);
        GLTextureViewport* glv = m_window->glViewport();
        if (glv) {
            connect(cursorMgr, &CursorManager::cursorChanged,
                    glv, qOverload<>(&QWidget::update));
        }
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
