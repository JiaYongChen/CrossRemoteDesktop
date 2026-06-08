#include "DecodeWorker.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QBuffer>
#include <QtCore/QDataStream>
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
        DecodeTask task;
        if (!m_queue.tryDequeue(task)) {
            QThread::msleep(1);
            continue;
        }

        const quint8 flags = task.screenData.flags;
        const bool isFullFrame = flags & static_cast<quint8>(ScreenDataFlags::FULL_FRAME);
        const bool isMoveRect  = flags & static_cast<quint8>(ScreenDataFlags::MOVE_RECT);

        if (!m_outputBuffer) {
            qCWarning(lcClient) << "DecodeWorker: outputBuffer is null";
            continue;
        }

        // Determine remoteSize
        QSize remoteSize = task.remoteSize;
        if (flags & static_cast<quint8>(ScreenDataFlags::SCALED)
            && task.screenData.originalWidth > 0) {
            remoteSize = QSize(task.screenData.originalWidth, task.screenData.originalHeight);
        } else if (!isFullFrame && m_compositorReady.load()) {
            QMutexLocker locker(&m_compositorMutex);
            remoteSize = m_compositorBuffer.size();
        }

        // === Move rect: memcpy pixels within compositor buffer ===
        if (isMoveRect && m_compositorReady.load()) {
            QDataStream ds(task.screenData.imageData);
            ds.setByteOrder(QDataStream::LittleEndian);
            qint32 srcX, srcY, dstX, dstY;
            quint16 mw, mh;
            ds >> srcX >> srcY >> dstX >> dstY >> mw >> mh;

            QMutexLocker locker(&m_compositorMutex);
            if (m_compositorBuffer.isNull()) { continue; }

            // Compute clamped source and destination rectangles
            const int copyW = qMin(static_cast<int>(mw),
                qMin(m_compositorBuffer.width() - srcX,
                     m_compositorBuffer.width() - dstX));
            const int copyH = qMin(static_cast<int>(mh),
                qMin(m_compositorBuffer.height() - srcY,
                     m_compositorBuffer.height() - dstY));
            if (copyW <= 0 || copyH <= 0) continue;

            // Row-by-row memcpy (handles overlapping regions correctly since
            // we read from source position which is the pre-move state)
            for (int row = 0; row < copyH; ++row) {
                const uchar* srcLine = m_compositorBuffer.constScanLine(srcY + row);
                uchar* dstLine = m_compositorBuffer.scanLine(dstY + row);
                memcpy(dstLine + dstX * 4, srcLine + srcX * 4,
                    static_cast<size_t>(copyW) * 4);
            }

            // Output to TripleBuffer with COW reference
            DecodedFrame* frame = nullptr;
            int idx = m_outputBuffer->acquireWrite(frame);
            if (frame) {
                quint64 fid = m_nextFrameId.fetch_add(1, std::memory_order_relaxed);
                frame->image      = m_compositorBuffer;  // COW reference
                frame->remoteSize = remoteSize;
                frame->frameId    = fid;
                frame->isFullFrame = false;
                frame->isMoveRect  = true;
                frame->dirtyRect   = QRect(dstX, dstY, copyW, copyH);
                frame->moveSrc     = QPoint(srcX, srcY);
                frame->moveDst     = QPoint(dstX, dstY);
                frame->moveSize    = QSize(copyW, copyH);
                m_outputBuffer->commitWrite(idx);
                emit frameDecoded(fid);
            }
            continue;
        }

        // === Decode image (WebP/PNG auto-detected) ===
        QBuffer buffer(&task.screenData.imageData);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        if (!reader.read(&m_decodeBuffer) || m_decodeBuffer.isNull()) {
            qCWarning(lcClient) << "DecodeWorker: image decode failed, size:"
                                << task.screenData.imageData.size();
            emit decodeError(QStringLiteral("图像解码失败"));
            continue;
        }

        const int regionX = task.screenData.x;
        const int regionY = task.screenData.y;

        // === Composite into buffer ===
        {
            QMutexLocker locker(&m_compositorMutex);

            if (isFullFrame) {
                // Full frame: initialize/replace compositor buffer
                m_compositorBuffer = m_decodeBuffer.copy();
                m_compositorReady.store(true);
                remoteSize = m_compositorBuffer.size();
            } else if (m_compositorReady.load()) {
                if (m_compositorBuffer.isNull()) {
                    continue;  // Defense: wait for first full frame
                }

                // Regional frame: paste decoded region into compositor buffer
                const int pasteW = qMin(m_decodeBuffer.width(),
                    m_compositorBuffer.width() - regionX);
                const int pasteH = qMin(m_decodeBuffer.height(),
                    m_compositorBuffer.height() - regionY);
                if (pasteW <= 0 || pasteH <= 0) continue;

                // Row-by-row copy (faster than QPainter for raw pixels)
                for (int row = 0; row < pasteH; ++row) {
                    const uchar* srcLine = m_decodeBuffer.constScanLine(row);
                    uchar* dstLine = m_compositorBuffer.scanLine(regionY + row);
                    memcpy(dstLine + regionX * 4, srcLine,
                        static_cast<size_t>(pasteW) * 4);
                }
            } else {
                // Compositor buffer not initialized yet — skip until first full frame
                continue;
            }
        }

        // Output to TripleBuffer
        DecodedFrame* frame = nullptr;
        int idx = m_outputBuffer->acquireWrite(frame);
        if (frame) {
            quint64 fid = m_nextFrameId.fetch_add(1, std::memory_order_relaxed);
            frame->image       = m_compositorBuffer;  // COW reference
            frame->remoteSize  = remoteSize;
            frame->frameId     = fid;
            frame->isFullFrame = isFullFrame;
            frame->dirtyRect   = isFullFrame
                ? QRect(QPoint(0,0), m_compositorBuffer.size())
                : QRect(regionX, regionY,
                    m_decodeBuffer.width(), m_decodeBuffer.height());
            m_outputBuffer->commitWrite(idx);
            emit frameDecoded(fid);
        }
    }

    qCInfo(lcClient) << "DecodeWorker::workLoop() - Decode loop ended";
    emit stopped();
}
