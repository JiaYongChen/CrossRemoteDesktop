// src/server/session/SessionQueuePair.h
#pragma once

#include "../../common/core/threading/ThreadSafeQueue.h"
#include "../dataflow/DataFlowStructures.h"
#include "../../common/core/config/ProcessingConstants.h"

/**
 * @brief 每 session 私有队列对
 *
 * 纯数据结构（非 QObject），由 ServerSession 拥有。
 * captureQueue：FrameBroadcaster 写入 → DataProcessingWorker 读取
 * processedQueue：DataProcessingWorker 写入 → ClientHandlerWorker 读取
 * 两个队列均为 Drain-to-Latest 语义。
 */
struct SessionQueuePair {
    ThreadSafeQueue<CapturedFrame>  captureQueue;
    ThreadSafeQueue<ProcessedData>  processedQueue;

    void initialize(int captureSize = ProcessingConstants::MaxQueueSize,
                    int processedSize = ProcessingConstants::MaxQueueSize) {
        captureQueue.setMaxSize(captureSize);
        processedQueue.setMaxSize(processedSize);
    }
};
