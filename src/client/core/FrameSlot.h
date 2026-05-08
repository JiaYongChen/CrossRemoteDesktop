#pragma once

#include <QtGui/QImage>
#include <QtCore/QSize>
#include <chrono>

struct FrameSlot {
    QImage image;
    QSize   remoteSize;
    std::chrono::steady_clock::time_point arrivalTs;
    quint64 frameId = 0;
};
