#pragma once

#include <QtCore/QSize>
#include <QtGui/QImage>

/**
 * @brief 解码后的帧，通过 TripleBuffer 从 DecodeThread 传递到 GUI 线程。
 *
 * 独立于 DecodeWorker 定义，因为 GLTextureViewport 和测试代码
 * 也需要直接使用此类型，不应依赖解码器的完整头文件。
 */
struct DecodedFrame {
    QImage   image;
    QSize    remoteSize;
    quint64  frameId = 0;
};
