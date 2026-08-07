#include "RemoteDesktopSession.h"

#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include "client/decode/GpuDecodeTarget.h"
#include "client/network/ConnectionManager.h"
#include "client/session/DecodePipeline.h"
#include "client/session/ProtocolSession.h"
#include "client/window/ClientRemoteWindow.h"
#include "client/window/ConnectionLifecycle.h"
#include "client/window/CursorManager.h"
#include "client/window/DragDropHandler.h"
#include "client/window/GLTextureViewport.h"
#include "client/window/InputForwarder.h"
#include "common/clipboard/ClipboardManager.h"
#include "common/config/SettingsManager.h"
#include "common/logging/LoggingCategories.h"
#include "common/network/Protocol.h"
#include "core/transfer/FileTransferManager.h"

RemoteDesktopSession::RemoteDesktopSession(const ConnectionParams& params,
                                           const QString& connectionId,
                                           SettingsManager& settings,
                                           QObject* parent)
    : QObject(parent)
    , m_connectionId(connectionId)
    , m_params(params)
    , m_settings(&settings) {

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
    m_connectionManager = new ConnectionManager(this, m_settings);

    // ── 预设认证凭证（在 connectToHost 前，handleHandshakeResponse 会自动使用）──
    m_connectionManager->setCredentials(m_params.username, m_params.password);

    // ── 网络参数配置 ──
    m_connectionManager->setConnectionTimeout(m_params.connectionTimeout);
    m_connectionManager->setAutoReconnect(m_params.autoReconnect);
    m_connectionManager->setReconnectInterval(m_params.reconnectInterval);

    // ── 显示参数（认证成功后经 SESSION_CAPABILITIES 携带到服务端）──
    m_connectionManager->setColorDepth(m_params.colorDepth);
    m_connectionManager->setImageQuality(m_params.imageQuality);

    // 所有客户端组件驻留 Main 线程（Network 线程已于架构简化中移除），
    // 设 this 为 parent 实现自动析构清理
    m_protocolSession = new ProtocolSession(m_connectionManager, m_decodePipeline, this);

    // 文件传输管理器（接收目录默认下载目录，可在 SettingsManager 配置覆盖）
    const QString downloadDir = m_settings
        ? m_settings->getString(QStringLiteral("fileTransfer.downloadDir"),
              QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
        : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    m_fileTransferManager = new FileTransferManager(downloadDir, this);

    // 客户端传输超时检测（与服务端 ServerService 对标）
    auto* timeoutTimer = new QTimer(this);
    connect(timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_fileTransferManager) m_fileTransferManager->checkTimeouts();
    });
    timeoutTimer->start(2000);
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
                // GL 上下文重建后重启管线（首次初始化阶段未认证，跳过）。
                // 判据用 isAuthenticated 而非 isActive：后者的「管线运行中」分量与
                // 「停止后重启」的前提自相矛盾（恒假），会使管线在一次上下文重建后永久停转
                if (m_connectionManager->isAuthenticated() && !pipeline->isRunning()) {
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

    m_window->show();
    m_window->raise();
    m_window->activateWindow();
    QApplication::processEvents();
}

void RemoteDesktopSession::wireSignals() {
    ConnectionLifecycle* lifecycle = m_window->connectionLifecycle();

    // 窗口销毁时置空裸指针：session 存活至 deleteLater 期间若迟到事件，
    // 其 lambda 中 m_window 访问不悬垂
    connect(m_window, &QObject::destroyed, this, [this] { m_window = nullptr; });

    // ── 管道事件 → ConnectionLifecycle（驱动窗口标题与终端处理）──
    connect(m_connectionManager, &ConnectionManager::connecting,
            lifecycle, &ConnectionLifecycle::onConnecting);
    connect(m_connectionManager, &ConnectionManager::connected,
            lifecycle, &ConnectionLifecycle::onConnected);
    connect(m_connectionManager, &ConnectionManager::disconnected,
            lifecycle, &ConnectionLifecycle::onDisconnected);
    connect(m_connectionManager, &ConnectionManager::reconnecting,
            lifecycle, &ConnectionLifecycle::onReconnecting);

    // ── 认证成功：启动会话 + 补发剪贴板 + 上报实际通过验证的凭据 ──
    connect(m_connectionManager, &ConnectionManager::authenticated,
            this, [this] {
                if ( !m_window ) return;
                m_protocolSession->startSession();
                if (ClipboardManager* clipboardMgr = m_window->findChild<ClipboardManager*>()) {
                    clipboardMgr->resync();
                }
                emit authenticated(m_connectionId, m_connectionManager->username(),
                                   m_connectionManager->password());
            });

    // ── 认证失败（预期结果，非系统故障）──
    connect(m_connectionManager, &ConnectionManager::authenticationFailed,
            this, [this, lifecycle](AuthResult result, const QString& message) {
                switch ( result ) {
                case AuthResult::INVALID_CREDENTIALS:
                    if ( m_authDialogPending ) return;  // 重入守卫：防 200ms 窗口期重复弹框
                    m_authDialogPending = true;
                    lifecycle->setAuthRetryPending(true);
                    QTimer::singleShot(200, m_window, [this, message]() {
                        if ( m_window ) showCredentialDialog(message);
                    });
                    break;
                case AuthResult::ACCESS_DENIED:
                    // 终态：提示锁定信息后静默关窗。无重试入口，MessageBox 确认即关窗
                    if ( m_authDialogPending ) return;
                    m_authDialogPending = true;
                    QTimer::singleShot(200, m_window, [this, message]() {
                        if ( !m_window ) { m_authDialogPending = false; return; }
                        QMessageBox::warning(m_window, tr("登录已锁定"), message);
                        m_authDialogPending = false;
                        m_window->close();
                    });
                    break;
                }
            });

    // ── 版本不匹配（握手版本闸门不通过）──
    connect(m_connectionManager, &ConnectionManager::versionMismatched,
            this, [this, lifecycle](const QString& serverVer, const QString& localVer) {
                // 同样挂起终端处理：versionMismatched 后连接立即断开，
                // 对话框 exec 期间不得触发关窗
                lifecycle->setAuthRetryPending(true);
                showVersionMismatchDialog(serverVer, localVer);
                lifecycle->setAuthRetryPending(false);
            });

    // ── 连接层故障 → UI + 上层（TcpClient 经 ConnectionManager 直传，保留中文诊断）──
    connect(m_connectionManager, &ConnectionManager::errorOccurred,
            this, [this](const RdError& error) {
                if ( m_window ) {
                    m_window->connectionLifecycle()->onErrorOccurred(error);
                }
                emit errorOccurred(error);
            });

    // ── 转发协议层错误 ──
    connect(m_protocolSession, &ProtocolSession::sessionError,
            this, &RemoteDesktopSession::errorOccurred);

    // ── 转发解码错误（DecodeWorker 失败流首帧上报）──
    connect(m_decodePipeline, &DecodePipeline::decodeError,
            this, &RemoteDesktopSession::errorOccurred);

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

        // ── 剪贴板文件 ──
        connect(clipboardMgr, &ClipboardManager::clipboardFilesChanged,
            m_protocolSession, &ProtocolSession::sendClipboardFiles);
        connect(m_protocolSession, &ProtocolSession::clipboardFilesReceived,
            clipboardMgr, &ClipboardManager::applyRemoteFiles);
    }

    // 注意：dragSource (flags=0x01) 的 FILE_LIST 目前仅作标识用途。
    // 拖出操作（startDragOut）需用户 UI 交互显式触发，不应在收到列表时自动弹出 QDrag。

    // ── 文件传输：数据收发 + ACK 回路 ──
    FileTransferManager* ftm = m_fileTransferManager;
    if (ftm) {
        // 发送方向：大文件块写入后回发 ACK（推进服务端滑动窗口）
        connect(ftm, &FileTransferManager::fileChunkAckNeeded,
                m_protocolSession, &ProtocolSession::sendFileTransferAck);

        // 接收方向：远端回发的文件数据块 → 本地 FileTransferManager 写入
        connect(m_protocolSession, &ProtocolSession::clipboardFileChunkReceived,
                ftm, [ftm](quint32 fileIndex, const QByteArray& data, quint8 flags) {
                    ftm->handleIncomingChunk(static_cast<int>(fileIndex), data,
                                             (flags & 0x01) != 0);
                });
        connect(m_protocolSession, &ProtocolSession::fileTransferChunkReceived,
                ftm, [ftm](quint32 fileIndex, quint32 seq, const QByteArray& data) {
                    ftm->handleIncomingChunk(static_cast<int>(fileIndex), data,
                                             false, seq);
                });
        connect(m_protocolSession, &ProtocolSession::fileTransferAckReceived,
                ftm, [ftm](quint32 fileIndex, quint32 ackSeq) {
                    ftm->handleAck(static_cast<int>(fileIndex), ackSeq);
                });
        connect(m_protocolSession, &ProtocolSession::fileTransferCancelled,
                ftm, [ftm](quint32 fileIndex) {
                    ftm->cancelTransfer(static_cast<int>(fileIndex));
                });

        // 发送方向：FileTransferManager 读出的数据块 → ProtocolSession 发送
        connect(ftm, &FileTransferManager::fileChunkReady,
                m_protocolSession,
                [this](int fileIndex, const QByteArray& data, quint32 seq, bool lastChunk) {
            if (lastChunk) {
                m_protocolSession->sendClipboardFileChunk(
                    static_cast<quint32>(fileIndex), data, true);
            } else {
                m_protocolSession->sendFileTransferChunk(
                    static_cast<quint32>(fileIndex), seq, data);
            }
        });

        // 发送方向：远端请求本机文件 → FileTransferManager 作为复制方读文件回发
        connect(m_protocolSession, &ProtocolSession::fileContentRequestReceived,
                ftm, [ftm, clipboardMgr](quint32 fileIndex) {
            const ClipboardFileList list = clipboardMgr->lastFileList();
            const QString path = clipboardMgr->lastFilePath(static_cast<int>(fileIndex));
            if (!path.isEmpty()) {
                const QFileInfo fi(path);
                ftm->handleFileRequest(static_cast<int>(fileIndex), list, path);
            }
        });
        connect(m_protocolSession, &ProtocolSession::fileTransferInitReceived,
                ftm, [ftm, clipboardMgr](quint32 fileIndex) {
            const ClipboardFileList list = clipboardMgr->lastFileList();
            const QString path = clipboardMgr->lastFilePath(static_cast<int>(fileIndex));
            if (!path.isEmpty()) {
                const QFileInfo fi(path);
                ftm->handleFileRequest(static_cast<int>(fileIndex), list, path);
            }
        });
    }

    // ── 拖放（本地文件拖入远程视口 → 标记 dragSource 后走剪贴板文件通道）──
    DragDropHandler* ddh = m_window->dragDropHandler();
    if (ddh) {
        connect(ddh, &DragDropHandler::filesDroppedToRemote,
            clipboardMgr, [clipboardMgr](const ClipboardFileList& files) {
                if (!clipboardMgr->isEnabled()) return;
                ClipboardFileList copy = files;
                copy.flags |= 0x01;
                emit clipboardMgr->clipboardFilesChanged(copy);
            });
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

    // 1.5. 终止所有进行中的文件传输（断连即取消，清理半成品文件）
    if (m_fileTransferManager) {
        m_fileTransferManager->cancelAllTransfers();
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

void RemoteDesktopSession::showCredentialDialog(const QString& errorMessage) {
    if (!m_window) return;

    // 不使用 m_window 作为父对象——m_window 有 WA_DeleteOnClose，
    // 嵌套事件循环中若窗口关闭会导致 Qt 对栈对象调用 delete（未定义行为）
    QDialog dialog(nullptr);
    dialog.setWindowTitle(tr("认证失败"));
    dialog.setMinimumWidth(320);

    auto* layout = new QVBoxLayout(&dialog);

    // 错误信息（红色）
    auto* errorLabel = new QLabel(errorMessage);
    errorLabel->setStyleSheet("color: #d32f2f; font-weight: bold;");
    layout->addWidget(errorLabel);

    // 用户名可编辑（服务端已统一失败响应，用户名/密码错误不可区分）
    layout->addWidget(new QLabel(tr("用户名:")));
    auto* usernameEdit = new QLineEdit(m_connectionManager->username());
    layout->addWidget(usernameEdit);

    // 密码（始终可编辑）
    layout->addWidget(new QLabel(tr("密码:")));
    auto* passwordEdit = new QLineEdit();
    passwordEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(passwordEdit);

    // 按钮
    auto* buttonLayout = new QHBoxLayout();
    auto* retryBtn = new QPushButton(tr("重试"));
    auto* cancelBtn = new QPushButton(tr("取消"));
    buttonLayout->addStretch();
    buttonLayout->addWidget(retryBtn);
    buttonLayout->addWidget(cancelBtn);
    layout->addLayout(buttonLayout);

    connect(retryBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    // 布局完成后定位到窗口中心（adjustSize 确保 rect 为实际尺寸而非默认值）
    dialog.adjustSize();
    if (m_window->isVisible()) {
        dialog.move(m_window->geometry().center() - dialog.rect().center());
    }

    if (dialog.exec() == QDialog::Accepted) {
        m_connectionManager->updateCredentials(usernameEdit->text().trimmed(),
                                               passwordEdit->text());
        m_connectionManager->connectToHost(m_params.host, m_params.port);
    } else {
        m_window->close();
    }

    m_window->connectionLifecycle()->setAuthRetryPending(false);
    m_authDialogPending = false;   // 解除重入守卫，允许下次失败重弹
}

void RemoteDesktopSession::showVersionMismatchDialog(const QString& serverVer,
                                                     const QString& localVer) {
    if (!m_window) return;

    QMessageBox msgBox(m_window);
    msgBox.setWindowTitle(tr("版本不兼容"));
    msgBox.setText(tr("远程主机版本与本机不兼容。"));
    msgBox.setInformativeText(tr("远程: %1\n本机: %2").arg(serverVer, localVer));
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();

    m_window->close();
}
