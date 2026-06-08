#pragma once

#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtGui/QImage>

/**
 * @brief 解码后的帧，通过 TripleBuffer 从 DecodeThread 传递到 GUI 线程。
 *
 * 独立于 DecodeWorker 定义，因为 GLTextureViewport 和测试代码
 * 也需要直接使用此类型，不应依赖解码器的完整头文件。
 */
struct DecodedFrame {
    QImage   image;         ///< 解码后图像（COW 引用 compositor buffer 或全帧）
    QSize    remoteSize;    ///< 远端桌面总尺寸
    QRect    dirtyRect;     ///< 本帧脏区域（全帧模式下 = image.rect()）
    quint64  frameId = 0;
    bool     isFullFrame = false;  ///< 全帧，客户端初始化 compositor buffer
    bool     isMoveRect  = false;  ///< 移动区域，跳过编解码
    QPoint   moveSrc;             ///< 移动源坐标
    QPoint   moveDst;             ///< 移动目标坐标
    QSize    moveSize;            ///< 移动区域大小
};
