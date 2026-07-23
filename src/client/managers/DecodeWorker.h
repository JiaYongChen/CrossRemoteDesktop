#pragma once

#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtGui/QImage>
#include <atomic>
#include <chrono>
#include "../../common/network/Protocol.h"
#include "error/RdError.h"
#include "../../common/threading/ThreadSafeQueue.h"
#include "../core/FrameSlot.h"
#include "../core/TripleBuffer.h"

#include <memory>
#include "../decode/IDecoder.h"

#include <QtGui/QOpenGLContext>
#include <QtGui/QOffscreenSurface>

class GLTextureViewport;
class GpuDecodeTarget;

class DecodeWorker : public QObject {
    Q_OBJECT

public:
    explicit DecodeWorker(QObject* parent = nullptr);
    ~DecodeWorker() override;

    /// 由 DecodePipeline 所在线程调用，投递待解码的帧（线程安全）。
    /// 接受 ScreenData 值传递，使用移动语义避免 QByteArray imageData 深拷贝。
    [[nodiscard]] bool enqueueFrame(ScreenData screenData, const QSize& remoteSize);

    /// 设置 TripleBuffer 指针（在 createDecodePipeline 中调用）
    void setFrameBuffer(TripleBuffer<FrameSlot>* buffer);

    /// 初始化 worker GL 上下文（在 DecodeThread 内调用）
    [[nodiscard]] bool initializeGL();

    /// 在 DecodeThread 上下文内安全删除 GL 对象，避免跨线程 QObject 删除断言。
    /// 必须在 decodeThread->quit() 之前通过 BlockingQueuedConnection 调用。
    void cleanupGL();

    /// 设置 GLTextureViewport 引用
    void setGLViewport(GLTextureViewport* vp);

    /// 设置 GPU 解码目标（构造后注入，用于获取 worker GL 上下文）
    void setDecodeTarget(GpuDecodeTarget* target);

    /// 停止队列
    void requestStop();

signals:
    /// 解码失败上报（仅在失败流的首帧发射一次，成功后重置——避免每帧风暴）
    void decodeError(const RdError& error);

public slots:
    void start();  // 启动工作循环

private slots:
    void workLoop();

private:
    /// @return true 表示处理了一帧，false 表示队列为空（用于调用方空闲退避）
    bool processOneFrame();

    /// 解码失败上报：失败流首帧发射 decodeError 信号，日志按 30 帧限速
    void reportDecodeFailure(const QString& reason);

    struct DecodeTask {
        ScreenData screenData;
        QSize      remoteSize;
        std::chrono::steady_clock::time_point enqueueTs;  ///< 入队时刻（诊断：队列等待计时起点）
    };

    ThreadSafeQueue<DecodeTask> m_queue{3};
    std::atomic<bool> m_running{false};
    /// 不可复活的停止闩锁：requestStop 置位后永不清除。
    /// 防止竞态——排队中的 start() 事件在 requestStop 之后才被派发时，
    /// 其无条件 m_running.store(true) 会复活工作循环导致 stop() 永久阻塞。
    std::atomic<bool> m_stopRequested{false};

    QOpenGLContext* m_glContext = nullptr;
    QOffscreenSurface* m_glSurface = nullptr;
    GLTextureViewport* m_glViewport = nullptr;

    TripleBuffer<FrameSlot>* m_frameBuffer = nullptr;

    std::unique_ptr<IDecoder> m_decoder;  ///< 解码器（运行时选择 TurboJpegDecoder 或 NvJpegDecoder）

    GpuDecodeTarget* m_decodeTarget = nullptr;  ///< GPU 解码目标（构造后注入，提供 worker GL 上下文）

    /// GL 初始化失败标志（initializeGL 失败时置为 true，start 据此选择解码路径）
    bool m_glInitFailed = false;

    // 诊断统计（成员变量：每实例独立，避免多会话 DecodeThread 间数据竞争）
    int    m_diagFrameCount = 0;
    qint64 m_queueWaitAccumUs = 0;
    qint64 m_queueWaitMaxUs = 0;
    qint64 m_decodeAccumUs = 0;
    qint64 m_decodeMaxUs = 0;

    /// 连续解码失败帧数（用于错误信号去重与日志限速）
    int m_decodeFailStreak = 0;
};
