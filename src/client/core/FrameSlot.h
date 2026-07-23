#pragma once

#include <QtGui/QImage>
#include <QtCore/QSize>
#include <chrono>

#include <QtGui/qopengl.h>

struct FrameSlot {
    QImage image;
    QSize   remoteSize;
    std::chrono::steady_clock::time_point arrivalTs;
    GLsync  uploadFence = nullptr;  // Set by worker thread when GL upload is done
};
