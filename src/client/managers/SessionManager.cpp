#include "SessionManager.h"
#include "../network/ConnectionManager.h"
#include "../../common/core/logging/LoggingCategories.h"
#include "../../common/core/network/Protocol.h"
#ifndef QT_NO_OPENGL
#include "../window/GLTextureViewport.h"
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLExtraFunctions>
#include <QtGui/QOffscreenSurface>
#endif
#include <QtCore/QBuffer>
#include <QtCore/QDataStream>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QMutexLocker>
#include <QtGui/QImageReader>
#include <algorithm>

SessionManager::SessionManager(const QString& connectionId, QObject* parent)
    : QObject(parent)
    , m_connectionId(connectionId)
    , m_connectionManager(new ConnectionManager(this))
    , m_statsTimer(new QTimer(this))
    , m_frameRate(60) {

    // SessionManager 拥有并管理 ConnectionManager
    setupConnections();

    // 设置性能统计定时器
    m_statsTimer->setInterval(UIConstants::STATS_UPDATE_INTERVAL);
    connect(m_statsTimer, &QTimer::timeout, this, &SessionManager::updatePerformanceStats);

    // 初始化统计数据
    resetStats();
}

SessionManager::~SessionManager() {
    terminateSession();

#ifndef QT_NO_OPENGL
    // GL 资源通常已在 shutdown 第一阶段由 cleanupGLResources() 在工作线程中清理。
    // 此处为防御性删除：如果 Phase 1 未执行（异常路径），直接 delete（不作
    // moveToThread，因为 moveToThread 只能从对象所属线程调用）。
    delete m_glContext;
    m_glContext = nullptr;
    delete m_glSurface;
    m_glSurface = nullptr;
#endif

    // ConnectionManager 由 Qt 父对象机制自动删除
    // QMutex 是栈上值类型，自动析构，无需 delete
}

QString SessionManager::connectionId() const {
    return m_connectionId;
}

void SessionManager::startSession() {
    if ( !m_connectionManager || !m_connectionManager->isAuthenticated() ) {
        qCWarning(lcClient) << "SessionManager::startSession() - Cannot start session, not authenticated";
        emit sessionError(tr("无法启动会话 - 未认证"));
        return;
    }

    // 重置统计数据
    resetStats();
    m_stats.sessionStartTime = QDateTime::currentDateTime();

    // 启动统计定时器
    m_statsTimer->start();
}

void SessionManager::suspendSession() {
    m_statsTimer->stop();
}

void SessionManager::resumeSession() {
    m_statsTimer->start();

    // 发送恢复会话的消息 (使用握手请求作为替代)
    if ( m_connectionManager && m_connectionManager->isAuthenticated() ) {
        m_connectionManager->sendMessage(MessageType::HANDSHAKE_REQUEST, BaseMessage());
    }
}

void SessionManager::terminateSession() {
    m_statsTimer->stop();

    // 注意：会话终止不发送断开请求，避免重复发送
    // 断开请求统一由 ConnectionManager/TcpClient 在 disconnectFromHost() 时发送
    // 此处只负责清理会话本地数据和状态

    // 清理会话数据
    m_remoteScreenSize = QSize();
    m_lastFpsTime = {};
    m_smoothedFrameDuration = 0.0;
}

void SessionManager::moveGLToThread(QThread* target) {
    Q_UNUSED(target);
#ifndef QT_NO_OPENGL
    if (m_glContext) m_glContext->moveToThread(target);
    if (m_glSurface) m_glSurface->moveToThread(target);
    m_glUploadReady = false;
#endif
}

bool SessionManager::isActive() const {
    return m_connectionManager && m_connectionManager->isAuthenticated() && m_statsTimer->isActive();
}

QSize SessionManager::remoteScreenSize() const {
    return m_remoteScreenSize;
}

void SessionManager::sendMouseEvent(int x, int y, int eventType) {
    if ( !isActive() || !m_connectionManager || !m_connectionManager->isAuthenticated() ) {
        return;
    }

    MouseEvent mouseEvent;
    mouseEvent.x = x;
    mouseEvent.y = y;
    mouseEvent.eventType = static_cast<MouseEventType>(eventType);
    mouseEvent.wheelDelta = 0;

    m_connectionManager->sendMessage(MessageType::MOUSE_EVENT, mouseEvent);
}

void SessionManager::sendKeyboardEvent(int key, int modifiers, bool pressed, const QString& text) {
    if ( !isActive() || !m_connectionManager || !m_connectionManager->isAuthenticated() ) {
        return;
    }

    KeyboardEvent keyEvent;
    keyEvent.keyCode = key;
    keyEvent.modifiers = modifiers;
    keyEvent.eventType = pressed ? KeyboardEventType::KEY_PRESS : KeyboardEventType::KEY_RELEASE;

    keyEvent.text = text;

    m_connectionManager->sendMessage(MessageType::KEYBOARD_EVENT, keyEvent);
}

void SessionManager::sendWheelEvent(int x, int y, int delta, int orientation) {
    if ( !isActive() || !m_connectionManager || !m_connectionManager->isAuthenticated() ) {
        return;
    }

    Q_UNUSED(orientation);

    MouseEvent wheelEvent;
    wheelEvent.x = x;
    wheelEvent.y = y;
    wheelEvent.eventType = delta > 0 ? MouseEventType::WHEEL_UP : MouseEventType::WHEEL_DOWN;
    wheelEvent.wheelDelta = delta;

    m_connectionManager->sendMessage(MessageType::MOUSE_EVENT, wheelEvent);
}

SessionManager::PerformanceStats SessionManager::performanceStats() const {
    return m_stats;
}

void SessionManager::resetStats() {
    m_stats.currentFPS = 0.0;
    m_stats.sessionStartTime = QDateTime();
    m_stats.frameCount = 0;
    m_lastFpsTime = {};
    m_smoothedFrameDuration = 0.0;
}

QString SessionManager::getFormattedPerformanceInfo() const {
    QStringList info;
    info << QString("FPS: %1").arg(m_stats.currentFPS, 0, 'f', 1);
    info << QString("Frame Rate: %1").arg(m_frameRate);

    if ( m_stats.sessionStartTime.isValid() ) {
        qint64 sessionDuration = m_stats.sessionStartTime.secsTo(QDateTime::currentDateTime());
        info << QString("Duration: %1s").arg(sessionDuration);
    }

    return info.join(" | ");
}

void SessionManager::setFrameRate(int fps) {
    m_frameRate = qBound(1, fps, 120);

    if ( isActive() && m_connectionManager && m_connectionManager->isAuthenticated() ) {
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << m_frameRate;
        // ConfigUpdate not available in protocol, using HANDSHAKE_REQUEST
        // m_connectionManager->sendMessage(MessageType::HANDSHAKE_REQUEST, data);
    }
}

int SessionManager::frameRate() const {
    return m_frameRate;
}

void SessionManager::onMessageReceived(MessageType type, const QByteArray& data) {
    switch ( type ) {
        case MessageType::SCREEN_DATA:
            // Fix 3: 仅在连接处于已连接状态时才处理屏幕数据。
            // ConnectionManager::isConnected() 已改为基于 socket 实际状态，
            // TCP 半关闭（ClosingState）期间会正确返回 false。
            if ( m_connectionManager && m_connectionManager->isConnected() ) {
                // 处理屏幕数据
                handleScreenData(data);
            }
            break;
        case MessageType::CURSOR_POSITION:
            // 处理光标位置数据
            handleCursorPosition(data);
            break;
        case MessageType::CLIPBOARD_DATA:
            // 处理剪贴板数据（文本或图片）
            handleClipboardData(data);
            break;
        default:
            // 其他消息类型忽略
            qCDebug(lcClient) << "SessionManager::onMessageReceived() - Unhandled message type:" << static_cast<quint32>(type);
            break;
    }
}

void SessionManager::updatePerformanceStats() {
    // 只更新FPS相关的统计信息，网络统计信息已移除
    emit performanceStatsUpdated(m_stats);
}

void SessionManager::setupConnections() {
    // 转发连接状态变化信号（用于 UI 更新）
    connect(m_connectionManager, &ConnectionManager::connectionStateChanged,
        this, &SessionManager::connectionStateChanged);

    // m_connectionManager 在构造函数中创建，永远不为空
    connect(m_connectionManager, &ConnectionManager::messageReceived,
        this, &SessionManager::onMessageReceived);
}

void SessionManager::calculateFPS() {
    // EMA 指数滑动平均：无 QQueue 迭代器开销，无 QDateTime 构造开销。
    // 公式: smoothed = α × instantFps + (1-α) × smoothed
    if ( m_smoothedFrameDuration > 0.0 ) {
        m_stats.currentFPS = 1.0 / m_smoothedFrameDuration;
    } else {
        m_stats.currentFPS = 0.0;
    }
}

void SessionManager::handleScreenData(const QByteArray& data) {
    // 使用正确的ScreenData结构体解码数据
    ScreenData screenData{};
    if ( !screenData.decode(data) ) {
        qCWarning(lcClient) << "SessionManager::handleScreenData() - Failed to decode ScreenData from received data, size:" << data.size();
        return;
    }

    // 验证数据完整性
    if ( screenData.imageData.isEmpty() || screenData.dataSize == 0 ) {
        qCWarning(lcClient) << "SessionManager::handleScreenData() - ScreenData contains empty image data";
        return;
    }

    if ( static_cast<quint32>(screenData.imageData.size()) != screenData.dataSize ) {
        qCWarning(lcClient) << "SessionManager::handleScreenData() - ScreenData size mismatch, expected:" << screenData.dataSize << "actual:" << screenData.imageData.size();
        return;
    }

    // 直接使用 JPEG 数据（服务端不再做 zstd 二次压缩）
    QByteArray jpegData = screenData.imageData;

    // 验证JPEG格式头部（JPEG文件以0xFF 0xD8开头）
    if ( jpegData.size() >= 2 ) {
        unsigned char byte0 = static_cast<unsigned char>(jpegData[0]);
        unsigned char byte1 = static_cast<unsigned char>(jpegData[1]);
        if ( byte0 != 0xFF || byte1 != 0xD8 ) {
            qCWarning(lcClient) << "SessionManager::handleScreenData() - Invalid JPEG header, first 2 bytes:"
                << QString("0x%1 0x%2")
                .arg(byte0, 2, 16, QChar('0'))
                .arg(byte1, 2, 16, QChar('0'));
        }
    }

    QByteArray frameData;
    {
        QMutexLocker locker(&m_frameDataMutex);
        // 使用解压后的JPEG数据
        frameData = jpegData;
        m_previousFrameData = jpegData;
    }

    // JPEG 解码到复用缓冲区，消除每帧 8MB malloc/free
    // （MSVC Debug 堆的 alloc/free 跟踪是"越来越慢"的根因）
    QBuffer buffer(&frameData);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "JPEG");
    reader.setAutoTransform(true);
    const bool loaded = reader.read(&m_decodeBuffer);
    QImage& image = m_decodeBuffer;

    if ( loaded && !image.isNull() ) {
        // Record the logical remote screen size for layout/aspect ratio.
        // If the server downscaled the frame, use the original dimensions
        // so that RenderManager::fitInView() computes the correct aspect ratio.
        // The actual upscale is NOT performed here — it was a redundant
        // SmoothTransformation that cost 5-10ms per frame without recovering
        // any lost detail. RenderManager's fitInView() handles display scaling.
        if ( screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED) ) {
            if ( screenData.originalWidth > 0 && screenData.originalHeight > 0 ) {
                m_remoteScreenSize = QSize(screenData.originalWidth, screenData.originalHeight);
            } else {
                m_remoteScreenSize = image.size();
            }
        } else {
            m_remoteScreenSize = image.size();
        }

        // 更新时间统计：用 std::chrono + EMA 替代 QQueue<QDateTime>，
        // 消除 Debug STL 迭代器检查和 QDateTime 构造开销。
        {
            const auto now = std::chrono::steady_clock::now();
            if ( m_lastFpsTime.time_since_epoch().count() != 0 ) {
                const double instant = std::chrono::duration<double>(now - m_lastFpsTime).count();
                if ( m_smoothedFrameDuration == 0.0 ) {
                    m_smoothedFrameDuration = instant;
                } else {
                    m_smoothedFrameDuration = kFpsAlpha * instant + (1.0 - kFpsAlpha) * m_smoothedFrameDuration;
                }
            }
            m_lastFpsTime = now;
        }
        m_stats.frameCount++;
        calculateFPS();

        // Triple-buffered lock-free frame delivery: write decoded frame
        // to the triple buffer. paintGL() reads on the GUI thread via atomics.
        FrameSlot* slot = nullptr;
        int idx = m_frameBuffer.acquireWrite(slot);
        if ( slot ) {
#ifndef QT_NO_OPENGL
            // Worker-side GL upload: decode → PBO memcpy → glTexSubImage2D
            // all on the worker thread. The GUI thread only waits for the
            // GLsync fence in paintGL() before drawing — no CPU memcpy in
            // the critical path.
            static int s_glUploadDiagCount = 0;
            if (m_glUploadReady && m_glContext && m_glSurface && m_glViewportForUpload) {
                // 生产者背压：paintGL 连续跳过 ≥3 帧时，隔帧跳过 GL 上传让 GPU 追赶。
                static int s_backoffCounter = 0;
                const bool gpuOverloaded = m_glViewportForUpload->consecutiveSkips() >= 3;
                const bool skipThisUpload = gpuOverloaded && (++s_backoffCounter % 2 == 0);
                if (gpuOverloaded && s_backoffCounter <= 3)
                    qCDebug(lcClient) << "GPU backpressure: skipping GL upload, skips="
                        << m_glViewportForUpload->consecutiveSkips();

                if (!skipThisUpload) {
                ++s_glUploadDiagCount;
                if ( s_glUploadDiagCount <= 3 || s_glUploadDiagCount % 100 == 0 )
                    qCDebug(lcClient) << "handleScreenData: GL upload #" << s_glUploadDiagCount << "frame" << m_stats.frameCount;
                m_glContext->makeCurrent(m_glSurface);
                // Delete any fence from previous frame before overwriting
                if (slot->uploadFence) {
                    auto* f = m_glContext->extraFunctions();
                    if (f) f->glDeleteSync(slot->uploadFence);
                    slot->uploadFence = nullptr;
                }
                GLsync fence = m_glViewportForUpload->uploadFromWorker(image);
                if (fence) {
                    slot->uploadFence = fence;
                }
                m_glContext->doneCurrent();
                } else {
                    // 背压触发：跳过 GL 上传，保留 image 供 GUI 线程回退上传
                    slot->image = image;
                }
            } else {
                static int s_glSkipDiagCount = 0;
                if ( ++s_glSkipDiagCount <= 3 ) {
                    qCWarning(lcClient) << "handleScreenData: GL upload skipped -"
                        << "ready:" << m_glUploadReady
                        << "ctx:" << (m_glContext != nullptr)
                        << "surface:" << (m_glSurface != nullptr)
                        << "viewport:" << (m_glViewportForUpload != nullptr);
                }
                // 仅在 GL 上传失败/未就绪时保留 image 供 GUI 线程回退上传
                slot->image = image;
            }
#else
            slot->image = image;
#endif
            slot->remoteSize = m_remoteScreenSize;
            slot->arrivalTs = std::chrono::steady_clock::now();
            slot->frameId = static_cast<quint64>(m_stats.frameCount);
            m_frameBuffer.commitWrite(idx);

#ifndef QT_NO_OPENGL
            // 请求 GUI 线程重绘（内部防重复排队）
            if ( m_glViewportForUpload ) {
                m_glViewportForUpload->requestRepaint();
            }
#endif
            }
    } else {
        qCWarning(lcClient) << "SessionManager::handleScreenData() - Failed to load JPEG image from frame data, size:" << frameData.size()
            << "first 16 bytes:" << frameData.left(16).toHex();
    }
}

void SessionManager::handleCursorPosition(const QByteArray& data) {
    // 使用 CursorMessage 解析光标类型数据
    CursorMessage message;
    if ( !message.decode(data) ) {
        qCWarning(lcClient) << "Failed to decode cursor type message";
        return;
    }

    // 仅发射光标类型更新信号
    emit remoteCursorTypeUpdated(message.cursorType);
}

QString SessionManager::currentHost() const {
    return m_connectionManager ? m_connectionManager->currentHost() : QString();
}

int SessionManager::currentPort() const {
    return m_connectionManager ? m_connectionManager->currentPort() : 0;
}

bool SessionManager::isConnected() const {
    return m_connectionManager && m_connectionManager->isConnected();
}

bool SessionManager::isAuthenticated() const {
    return m_connectionManager && m_connectionManager->isAuthenticated();
}

void SessionManager::connectToHost(const QString& host, int port) {
    if ( m_connectionManager ) {
        m_connectionManager->connectToHost(host, port);
    }
}

void SessionManager::disconnectFromHost() {
    if ( m_connectionManager ) {
        m_connectionManager->disconnectFromHost();
    }
}

void SessionManager::resetConnection() {
    // 1) 清理帧数据缓存（QMutexLocker 保证线程安全）
    {
        QMutexLocker locker(&m_frameDataMutex);
        m_previousFrameData.clear();
    }

    // 2) 清理帧时间队列
    m_lastFpsTime = {};
    m_smoothedFrameDuration = 0.0;

    // 3) 重置远程屏幕尺寸
    m_remoteScreenSize = QSize();

    // 4) 重置性能统计（包含 FPS、帧计数等）
    resetStats();

    // 5) 重置 TripleBuffer 索引，防止 GUI 线程读取上一次连接的过时帧
    m_frameBuffer.reset();

    // 6) 通知外部（UI、RenderManager 等）连接已重置
    emit connectionReset();

    qCInfo(lcClient) << "SessionManager::resetConnection() - Connection state reset complete";
}

// ==================== 剪贴板同步实现 ====================

void SessionManager::sendClipboardText(const QString& text) {
    if ( !m_connectionManager || !isConnected() ) {
        return;
    }

    ClipboardMessage message(text);
    m_connectionManager->sendMessage(MessageType::CLIPBOARD_DATA, message);
    
    qCDebug(lcClient) << "SessionManager::sendClipboardText() - Sent clipboard text, length:" << text.length();
}

void SessionManager::sendClipboardImage(const QByteArray& imageData, quint32 width, quint32 height) {
    if ( !m_connectionManager || !isConnected() ) {
        return;
    }

    ClipboardMessage message(imageData, width, height);
    m_connectionManager->sendMessage(MessageType::CLIPBOARD_DATA, message);
    
    qCDebug(lcClient) << "SessionManager::sendClipboardImage() - Sent clipboard image, size:" << width << "x" << height << "data size:" << imageData.size();
}

void SessionManager::handleClipboardData(const QByteArray& data) {
    ClipboardMessage message;
    if ( !message.decode(data) ) {
        qCWarning(lcClient) << "SessionManager::handleClipboardData() - Failed to decode clipboard message";
        return;
    }

    if (message.isText()) {
        qCDebug(lcClient) << "SessionManager::handleClipboardData() - Received clipboard text, length:" << message.text().length();
        emit clipboardTextReceived(message.text());
    } else if (message.isImage()) {
        qCDebug(lcClient) << "SessionManager::handleClipboardData() - Received clipboard image, size:" << message.width << "x" << message.height << "data size:" << message.imageData().size();
        emit clipboardImageReceived(message.imageData());
    }
}

#ifndef QT_NO_OPENGL
void SessionManager::initializeGLUpload(QOpenGLContext* shareContext) {
    if (!shareContext) {
        qCWarning(lcClient) << "SessionManager: No share context provided for GL upload";
        return;
    }

    // 原生 GL 资源必须在 GUI 线程创建（QOffscreenSurface 内部使用 QWindow）
    // 创建后通过 moveToThread 转移到 Worker 线程使用
    m_glContext = new QOpenGLContext();
    m_glContext->setShareContext(shareContext);
    m_glContext->setFormat(shareContext->format());
    if (!m_glContext->create()) {
        qCWarning(lcClient) << "SessionManager: Failed to create shared GL context";
        delete m_glContext;
        m_glContext = nullptr;
        return;
    }

    m_glSurface = new QOffscreenSurface();
    m_glSurface->setFormat(m_glContext->format());
    m_glSurface->create();

    // 转移到 SessionManager 所在的 Worker 线程，之后 handleScreenData 可在该线程调 makeCurrent
    m_glContext->moveToThread(this->thread());
    m_glSurface->moveToThread(this->thread());

    m_glUploadReady = true;
    qCInfo(lcClient) << "SessionManager: Shared GL context initialized for worker-thread texture upload";
}
#endif

