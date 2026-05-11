#pragma once

#include <QtGui/QImage>
#include <QtCore/QSize>
#include <QtGui/qopengl.h>
#include <chrono>

struct FrameSlot {
    QImage image;
    QSize   remoteSize;
    std::chrono::steady_clock::time_point arrivalTs;
    quint64 frameId = 0;
    GLsync  uploadFence = nullptr;  // Set by worker thread when GL upload is done
};
