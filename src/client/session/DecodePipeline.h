#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QSize>

#include "../core/TripleBuffer.h"
#include "../core/FrameSlot.h"
#include "../decode/IDecoder.h"
#include "../../common/network/Protocol.h"
#include "error/RdError.h"

class DecodeWorker;
class QThread;

#ifndef QT_NO_OPENGL
class GLTextureViewport;
class GpuDecodeTarget;
class QOpenGLContext;
#endif

/**
 * @brief 解码管线 — 封装 DecodeWorker 生命周期、DecodeThread 管理、TripleBuffer 所有权
 *
 * 归属 Network 线程（与 ProtocolSession 同线程）。
 * TripleBuffer 在构造后立即可用（原子操作，Main 线程安全读取）。
 */
class DecodePipeline : public QObject {
    Q_OBJECT
public:
    explicit DecodePipeline(const QString& connectionId, QObject* parent = nullptr);
    ~DecodePipeline() override;

    /// 帧缓冲 — 构造后立即可用，供 GLTextureViewport 通过 attachFrameBuffer() 挂载
    TripleBuffer<FrameSlot>* frameBuffer() { return &m_frameBuffer; }

    /// 启动管线：创建 DecodeThread → DecodeWorker → 注入 GL 上下文 → 启动工作循环
    void start();

    /// 停止管线（幂等）：停止 Worker → 清理 GL → 停线程 → 销毁 Worker
    Q_INVOKABLE void stop();

    /// 是否正在运行
    bool isRunning() const;

    /// 帧入队（线程安全，由 ProtocolSession 同线程直接调用）
    bool enqueueFrame(ScreenData data, const QSize& remoteSize);

#ifndef QT_NO_OPENGL
    /// GL 上下文注入（在 start() 之前从 Main 线程调用）
    void setGLContext(QOpenGLContext* context);
    /// GpuDecodeTarget 注入（在 start() 之前从 Main 线程调用）
    void setDecodeTarget(GpuDecodeTarget* target);
    /// GLTextureViewport 注入（在 start() 之前从 Main 线程调用）
    void setGLViewport(GLTextureViewport* vp);
#endif

signals:
    void decodeError(const RdError& error);
    void stopped();

private:
    QString                  m_connectionId;
    TripleBuffer<FrameSlot>  m_frameBuffer;   ///< 管线自有帧缓冲
    DecodeWorker*            m_worker = nullptr;
    bool                     m_running = false;

#ifndef QT_NO_OPENGL
    QOpenGLContext*    m_pendingGLContext = nullptr;
    GpuDecodeTarget*   m_decodeTarget = nullptr;
    GLTextureViewport* m_glViewportForUpload = nullptr;
#endif
};
