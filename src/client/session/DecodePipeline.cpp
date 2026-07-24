#include "DecodePipeline.h"

#include <memory>

#include <QtCore/QCoreApplication>
#include <QtCore/QDeadlineTimer>
#include <QtCore/QSemaphore>
#include <QtCore/QThread>
#include <QtGui/QOpenGLContext>

#include "client/decode/GpuDecodeTarget.h"
#include "client/managers/DecodeWorker.h"
#include "client/window/GLTextureViewport.h"
#include "common/logging/LoggingCategories.h"

DecodePipeline::DecodePipeline(const QString& connectionId, QObject* parent)
    : QObject(parent)
    , m_connectionId(connectionId) {
}

DecodePipeline::~DecodePipeline() {
    stop();
}

void DecodePipeline::start() {
    if (m_running) {
        qCDebug(lcClientSessionDecode) << "DecodePipeline::start() — already running for" << m_connectionId;
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

    // 先注入解码目标/视口，再排队 GL 初始化——postEvent 的内部同步保证
    // DecodeThread 消费 initializeGL 时 m_decodeTarget 写入已可见，
    // 消除"排队先于注入"的时序竞态
    if (m_decodeTarget) {
        m_worker->setDecodeTarget(m_decodeTarget);
    }
    m_worker->setGLViewport(m_glViewportForUpload);

    // GL 就绪时在 DecodeThread 内初始化 worker 上下文（由 GpuDecodeTarget 自建）
    if (m_glReady) {
        QMetaObject::invokeMethod(m_worker, [w = m_worker]() {
            if (!w->initializeGL()) {
                qCWarning(lcClientSessionDecode)
                    << "DecodePipeline: worker GL 初始化失败，回退 CPU 上传路径";
            }
        }, Qt::QueuedConnection);
    }

    // 注意：不设置 decodeThread 的 parent（跨线程限制），
    // 由 stop() 负责显式清理 decodeThread

    // 解码错误转发（跨线程 QueuedConnection，RdError 已注册元类型）
    connect(m_worker, &DecodeWorker::decodeError, this, &DecodePipeline::decodeError);

    // 启动工作循环
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection);

    m_running = true;
    qCInfo(lcClientSessionDecode) << "DecodePipeline::start() — started for" << m_connectionId;
}

void DecodePipeline::stop() {
    if (!m_worker || m_stopping) {
        return;
    }
    m_stopping = true;

    qCInfo(lcClientSessionDecode) << "DecodePipeline::stop() — stopping pipeline for" << m_connectionId;

    // 1. 停止队列
    m_worker->requestStop();

    // 2. 获取线程引用
    QThread* decodeThread = m_worker->thread();

    // 3. 在线程内清理 GL 资源（必须在 quit 之前）。
    //    不用 BlockingQueuedConnection：解码线程此刻可能正阻塞在
    //    ensureWorkerContext 的反向 BlockingQueuedConnection（等 Main 执行
    //    doneCurrent），双向阻塞即 ABBA 死锁。改为排队 + 信号量 + 泵事件，
    //    Main 在等待期间仍能服务解码线程的阻塞调用。
    if (decodeThread && decodeThread->isRunning()) {
        auto cleanupDone = std::make_shared<QSemaphore>();
        QMetaObject::invokeMethod(m_worker, [w = m_worker, cleanupDone]() {
            w->cleanupGL();
            cleanupDone->release();
        }, Qt::QueuedConnection);

        QDeadlineTimer deadline(3000);
        while (!cleanupDone->tryAcquire(1, 10)) {
            if (deadline.hasExpired()) {
                qCWarning(lcClientSessionDecode)
                    << "DecodePipeline::stop() — cleanupGL 等待超时，跳过（GL 资源随上下文销毁回收）";
                break;
            }
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }

    // 4. 停止线程
    if (decodeThread && decodeThread->isRunning()) {
        decodeThread->quit();
        if (!decodeThread->wait(3000)) {
            qCWarning(lcClientSessionDecode) << "DecodePipeline::stop() — thread quit timeout, requesting interruption";
            decodeThread->requestInterruption();
            decodeThread->quit();
            if (!decodeThread->wait(1000)) {
                // 线程彻底卡死（如驱动级阻塞）：不删除 worker/thread 避免 UAF。
                // 资源会泄漏但进程不会崩溃——这是两害相权取其轻。
                qCCritical(lcClientSessionDecode)
                    << "DecodePipeline::stop() — 解码线程无响应，跳过清理避免崩溃（资源泄漏）";
                m_worker = nullptr;      // 放弃所有权，不解引用
                decodeThread = nullptr;  // 放弃所有权，不删除
                m_running = false;
                m_stopping = false;
                return;
            }
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
    m_stopping = false;

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

void DecodePipeline::notifyGLReady() {
    // worker 上下文由 GpuDecodeTarget 在解码线程内自建，此方法仅标记 GL 已就绪
    m_glReady = true;
}

void DecodePipeline::setDecodeTarget(GpuDecodeTarget* target) {
    m_decodeTarget = target;
}

void DecodePipeline::setGLViewport(GLTextureViewport* vp) {
    m_glViewportForUpload = vp;
}
