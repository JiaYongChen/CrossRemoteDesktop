#pragma once

#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtGui/QImage>
#include <atomic>
#include <chrono>
#include "../../common/core/network/Protocol.h"
#include "error/RdError.h"
#include "../../common/core/threading/ThreadSafeQueue.h"
#include "../core/FrameSlot.h"
#include "../core/TripleBuffer.h"

#ifndef QT_NO_OPENGL
#include <QtGui/QOpenGLContext>
#include <QtGui/QOffscreenSurface>
#endif

class GLTextureViewport;

class DecodeWorker : public QObject {
    Q_OBJECT

public:
    explicit DecodeWorker(QObject* parent = nullptr);
    ~DecodeWorker() override;

    /// 由 SessionManager 在 SessionThread 调用，投递待解码的帧（线程安全）。
    /// 接受 ScreenData 值传递，使用移动语义避免 QByteArray imageData 深拷贝。
    [[nodiscard]] bool enqueueFrame(ScreenData screenData, const QSize& remoteSize);

    /// 设置 TripleBuffer 指针（在 createDecodePipeline 中调用）
    void setFrameBuffer(TripleBuffer<FrameSlot>* buffer);

#ifndef QT_NO_OPENGL
    /// 初始化共享 GL 上下文（在 GUI 线程调用，内部 moveToThread）
    bool initializeGL(QOpenGLContext* shareContext);

    /// 将 GL 对象移到目标线程（用于安全析构）
    void moveGLToThread(QThread* target);

    /// 在 DecodeThread 上下文内安全删除 GL 对象，避免跨线程 QObject 删除断言。
    /// 必须在 decodeThread->quit() 之前通过 BlockingQueuedConnection 调用。
    void cleanupGL();

    /// 设置 GLTextureViewport 引用
    void setGLViewport(GLTextureViewport* vp);
#endif

    /// 停止队列
    void requestStop();

    /// 是否正在运行
    bool isRunning() const { return m_running.load(); }

signals:
    void decodeError(const RdError& message);
    void stopped();

public slots:
    void start();  // 启动工作循环

private slots:
    void workLoop();

private:
    void processOneFrame();

    struct DecodeTask {
        ScreenData screenData;
        QSize      remoteSize;
    };

    ThreadSafeQueue<DecodeTask> m_queue{3};
    std::atomic<bool> m_running{false};

#ifndef QT_NO_OPENGL
    QOpenGLContext* m_glContext = nullptr;
    QOffscreenSurface* m_glSurface = nullptr;
    bool m_glUploadReady = false;
    GLTextureViewport* m_glViewport = nullptr;
#endif

    TripleBuffer<FrameSlot>* m_frameBuffer = nullptr;

    // JPEG 解码缓冲区复用
    QImage m_decodeBuffer;

    // 帧计数
    int m_frameId = 0;
};
