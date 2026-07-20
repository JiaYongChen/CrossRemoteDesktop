#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QSize>

#include "../core/TripleBuffer.h"
#include "../core/FrameSlot.h"
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
 * 归属 Main 线程（与 ProtocolSession 同线程），仅解码在独立 DecodeThread 进行。
 * TripleBuffer 在构造后立即可用（原子操作，跨线程安全读取）。
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
    /// GL 就绪通知（在 start() 之前从 Main 线程调用）。
    /// worker GL 上下文由 GpuDecodeTarget 在解码线程内自建，无需传入上下文指针
    void notifyGLReady();
    /// GpuDecodeTarget 注入（在 start() 之前从 Main 线程调用）
    void setDecodeTarget(GpuDecodeTarget* target);
    /// GLTextureViewport 注入（在 start() 之前从 Main 线程调用）
    void setGLViewport(GLTextureViewport* vp);
#endif

signals:
    /// 解码错误转发（DecodeWorker → 本信号 → RemoteDesktopSession::errorOccurred → UI）
    void decodeError(const RdError& error);

private:
    QString                  m_connectionId;
    TripleBuffer<FrameSlot>  m_frameBuffer;   ///< 管线自有帧缓冲
    DecodeWorker*            m_worker = nullptr;
    bool                     m_running = false;
    bool                     m_stopping = false;  ///< 重入守卫：processEvents 泵事件期间防二次进入

#ifndef QT_NO_OPENGL
    bool               m_glReady = false;  ///< GL 是否已就绪（glContextReady 已触发）
    GpuDecodeTarget*   m_decodeTarget = nullptr;
    GLTextureViewport* m_glViewportForUpload = nullptr;
#endif
};
