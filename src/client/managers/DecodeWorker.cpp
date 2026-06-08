#include "DecodeWorker.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QBuffer>
#include <QtGui/QImageReader>
#include <QtCore/QThread>

// ---- 构造/析构 ----

DecodeWorker::DecodeWorker(QObject* parent)
    : QObject(parent) {
}

DecodeWorker::~DecodeWorker() {
    requestStop();
}

// ---- 公共接口 ----

bool DecodeWorker::enqueueFrame(ScreenData screenData, const QSize& remoteSize) {
    if (!m_running.load()) {
        return false;
    }
    DecodeTask task;
    task.screenData = std::move(screenData);
    task.remoteSize = remoteSize;
    return m_queue.tryEnqueue(std::move(task));
}

void DecodeWorker::setOutputBuffer(TripleBuffer<DecodedFrame>* buf) {
    m_outputBuffer = buf;
}

void DecodeWorker::requestStop() {
    m_running.store(false);
}

void DecodeWorker::start() {
    m_running.store(true);
    // start() 通过 QueuedConnection 在 DecodeThread 中调用
    workLoop();
}

// ---- 工作循环 ----

void DecodeWorker::workLoop() {
    qCInfo(lcClient) << "DecodeWorker::workLoop() - Starting decode loop";

    while (m_running.load()) {
        // 1. 出队
        DecodeTask task;
        if (!m_queue.tryDequeue(task)) {
            // 队列空 — 退避 1ms 避免 CPU 空转
            QThread::msleep(1);
            continue;
        }

        // 2. JPEG 解码
        QBuffer buffer(&task.screenData.imageData);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "JPEG");
        reader.setAutoTransform(true);
        if (!reader.read(&m_decodeBuffer) || m_decodeBuffer.isNull()) {
            qCWarning(lcClient) << "DecodeWorker: JPEG decode failed, size:"
                                << task.screenData.imageData.size();
            emit decodeError(QStringLiteral("JPEG 解码失败"));
            continue;
        }

        // 确定 remoteSize：非缩放场景使用解码后的图像尺寸
        QSize remoteSize = task.remoteSize;
        if (!(task.screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED))
            || task.screenData.originalWidth == 0) {
            remoteSize = m_decodeBuffer.size();
        }

        // 3. 输出到 TripleBuffer（移动语义避免深拷贝）
        if (!m_outputBuffer) {
            qCWarning(lcClient) << "DecodeWorker: outputBuffer is null, dropping frame";
            continue;
        }

        DecodedFrame* frame = nullptr;
        int idx = m_outputBuffer->acquireWrite(frame);
        if (frame) {
            quint64 fid = m_nextFrameId.fetch_add(1, std::memory_order_relaxed);
            frame->image      = std::move(m_decodeBuffer);
            frame->remoteSize = remoteSize;
            frame->frameId    = fid;
            m_outputBuffer->commitWrite(idx);
            emit frameDecoded(fid);
        }
        // TripleBuffer 无空闲槽时覆盖最旧未读槽（latest-wins 语义），
        // 而非丢弃新帧。acquireWrite 永远返回有效索引。
    }

    qCInfo(lcClient) << "DecodeWorker::workLoop() - Decode loop ended";
    emit stopped();
}
