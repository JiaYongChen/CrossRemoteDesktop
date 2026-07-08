// src/server/session/ServerSession.h
// 临时桩文件 — 将在后续任务中被完整实现替换
#pragma once

#include <QtCore/QObject>

struct CapturedFrame;

class ServerSession : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void enqueueFrame(const CapturedFrame& frame) { Q_UNUSED(frame); }
};
