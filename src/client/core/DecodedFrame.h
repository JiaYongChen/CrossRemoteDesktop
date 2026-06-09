#pragma once

#include <QtCore/QSize>
#include <QtGui/QImage>

struct DecodedFrame {
    QImage   image;
    QSize    remoteSize;
    quint64  frameId = 0;
};
