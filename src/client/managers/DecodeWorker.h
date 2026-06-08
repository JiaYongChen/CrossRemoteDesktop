#pragma once

#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtGui/QImage>
#include <atomic>

#include "../../common/core/network/Protocol.h"
#include "../../common/core/threading/ThreadSafeQueue.h"
#include "../core/TripleBuffer.h"
#include "../core/DecodedFrame.h"

/**
 * @brief 纯 CPU 解码 Worker — JPEG 解码后通过 TripleBuffer 输出到 GUI 线程。
 *
 * 不再触碰 GL。DecodeThread 上运行 workLoop()，产出 DecodedFrame
 * 放入 TripleBuffer，emit frameDecoded 通知 GUI 线程做 GPU 上传。
 */
class DecodeWorker : public QObject {
    Q_OBJECT

public:
    explicit DecodeWorker(QObject* parent = nullptr);
    ~DecodeWorker() override;

    /// 由 SessionManager 调用，投递待解码的帧（线程安全）
    bool enqueueFrame(ScreenData screenData, const QSize& remoteSize);

    /// 设置输出 TripleBuffer 指针
    void setOutputBuffer(TripleBuffer<DecodedFrame>* buf);

    /// 停止工作循环
    void requestStop();

    /// 是否正在运行
    bool isRunning() const { return m_running.load(); }

signals:
    void decodeError(const QString& message);
    void stopped();
    /// 新帧已写入 TripleBuffer，通知 GUI 线程做 PreRender
    void frameDecoded(quint64 frameId);

public slots:
    void start();

private slots:
    void workLoop();

private:
    struct DecodeTask {
        ScreenData screenData;
        QSize      remoteSize;
    };

    ThreadSafeQueue<DecodeTask> m_queue{4};
    TripleBuffer<DecodedFrame>* m_outputBuffer = nullptr;
    QImage m_decodeBuffer;
    QImage m_compositorBuffer;         ///< 全尺寸远端桌面缓冲
    mutable QMutex m_compositorMutex;   ///< 保护 compositor buffer 写入
    std::atomic<bool> m_compositorReady{false}; ///< 是否已初始化（收到首全帧后为 true）
    std::atomic<quint64> m_nextFrameId{1};
    std::atomic<bool> m_running{false};
};
