#pragma once

#include <QtGui/QImage>
#include <QtCore/QSize>
#include <chrono>

#ifndef QT_NO_OPENGL
#include <QtGui/qopengl.h>
#endif

struct FrameSlot {
    QImage image;
    QSize   remoteSize;
    std::chrono::steady_clock::time_point arrivalTs;
#ifndef QT_NO_OPENGL
    GLsync  uploadFence = nullptr;  // Set by worker thread when GL upload is done
#endif
};
