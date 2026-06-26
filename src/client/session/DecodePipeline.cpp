#include "DecodePipeline.h"
#include "../managers/DecodeWorker.h"
#include "../../common/core/logging/LoggingCategories.h"

#ifndef QT_NO_OPENGL
#include "../window/GLTextureViewport.h"
#include "../decode/GpuDecodeTarget.h"
#include <QtGui/QOpenGLContext>
#endif

#include <QtCore/QThread>

DecodePipeline::DecodePipeline(const QString& connectionId, QObject* parent)
    : QObject(parent)
    , m_connectionId(connectionId) {
}

DecodePipeline::~DecodePipeline() {
    stop();
}

void DecodePipeline::start() {
    if (m_running) {
        qCWarning(lcClientSessionDecode) << "DecodePipeline::start() — already running for" << m_connectionId;
        return;
    }

    // 创建 DecodeThread
    QThread* decodeThread = new QThread();
    decodeThread->setObjectName(QString("DecodeThread-%1").arg(m_connectionId));
    decodeThread->start();

    // 创建 DecodeWorker
    m_worker = new DecodeWorker(nullptr);
    m_worker->moveToThread(decodeThread);
    m_worker->setFrameBuffer(&m_frameBuffer);

#ifndef QT_NO_OPENGL
    // 注入 GL 上下文（通过 QueuedConnection 在 DecodeThread 中执行）
    if (m_pendingGLContext && m_pendingGLContext->isValid()) {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, ctx = m_pendingGLContext]() {
            w->initializeGL(ctx);
        }, Qt::QueuedConnection);
    }
    if (m_decodeTarget) {
        m_worker->setDecodeTarget(m_decodeTarget);
    }
    m_worker->setGLViewport(m_glViewportForUpload);
#endif

    // 注意：不设置 decodeThread 的 parent（跨线程限制），
    // 由 stop() 负责显式清理 decodeThread

    // 转发信号
    connect(m_worker, &DecodeWorker::decodeError, this, &DecodePipeline::decodeError);
    connect(m_worker, &DecodeWorker::stopped, this, &DecodePipeline::stopped);

    // 启动工作循环
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection);

    m_running = true;
    qCInfo(lcClientSessionDecode) << "DecodePipeline::start() — started for" << m_connectionId;
}

void DecodePipeline::stop() {
    if (!m_worker) {
        return;
    }

    qCInfo(lcClientSessionDecode) << "DecodePipeline::stop() — stopping pipeline for" << m_connectionId;

    // 1. 停止队列
    m_worker->requestStop();

    // 2. 获取线程引用
    QThread* decodeThread = m_worker->thread();

    // 3. 在线程内清理 GL 资源（必须在 quit 之前）
#ifndef QT_NO_OPENGL
    if (decodeThread && decodeThread->isRunning()) {
        QMetaObject::invokeMethod(m_worker, [w = m_worker]() {
            w->cleanupGL();
        }, Qt::BlockingQueuedConnection);
    }
#endif

    // 4. 停止线程
    if (decodeThread && decodeThread->isRunning()) {
        decodeThread->quit();
        if (!decodeThread->wait(3000)) {
            qCWarning(lcClientSessionDecode) << "DecodePipeline::stop() — thread quit timeout, forcing";
            decodeThread->requestInterruption();
            decodeThread->quit();
            decodeThread->wait(1000);
        }
    }

    // 5. 删除 worker（可能会触发 ThreadSafeQueue 析构等清理）
    delete m_worker;
    m_worker = nullptr;

    // 6. 等待线程退出（worker 已删除，线程应快速退出）
    if (decodeThread && decodeThread->isRunning()) {
        if (!decodeThread->wait(1000)) {
            qCWarning(lcClientSessionDecode) << "DecodePipeline::stop() — thread still running after worker deleted, forcing";
            decodeThread->requestInterruption();
            decodeThread->quit();
            decodeThread->wait(1000);
        }
    }

    // 7. 清理线程（不设 parent 时需显式 delete）
    if (decodeThread) {
        delete decodeThread;
        decodeThread = nullptr;
    }

    m_running = false;

    qCInfo(lcClientSessionDecode) << "DecodePipeline::stop() — stopped for" << m_connectionId;
}

bool DecodePipeline::isRunning() const {
    return m_running;
}

bool DecodePipeline::enqueueFrame(ScreenData data, const QSize& remoteSize) {
    if (!m_worker || !m_running) {
        return false;
    }
    return m_worker->enqueueFrame(std::move(data), remoteSize);
}

#ifndef QT_NO_OPENGL
void DecodePipeline::setGLContext(QOpenGLContext* context) {
    m_pendingGLContext = context;
}

void DecodePipeline::setDecodeTarget(GpuDecodeTarget* target) {
    m_decodeTarget = target;
}

void DecodePipeline::setGLViewport(GLTextureViewport* vp) {
    m_glViewportForUpload = vp;
}
#endif
