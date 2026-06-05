#pragma once

#include <QtCore/QRect>
#include <chrono>

/**
 * @brief 捕获统计信息结构体
 * 
 * 用于记录和传递屏幕捕获的性能统计数据。
 */
struct CaptureStats {
    quint64 totalFramesCaptured = 0;       ///< 总捕获帧数
    quint64 droppedFrames = 0;             ///< 丢弃帧数
    double currentFrameRate = 0.0;         ///< 当前帧率
    std::chrono::milliseconds avgCaptureTime{0}; ///< 平均捕获时间
    std::chrono::milliseconds maxCaptureTime{0}; ///< 最大捕获时间
    std::chrono::milliseconds minCaptureTime{999999}; ///< 最小捕获时间
    double cpuUsage = 0.0;                 ///< CPU使用率
    quint64 memoryUsage = 0;               ///< 内存使用量
    
    /**
     * @brief 重置统计数据
     */
    void reset() {
        totalFramesCaptured = 0;
        droppedFrames = 0;
        currentFrameRate = 0.0;
        avgCaptureTime = std::chrono::milliseconds{0};
        maxCaptureTime = std::chrono::milliseconds{0};
        minCaptureTime = std::chrono::milliseconds{999999};
        cpuUsage = 0.0;
        memoryUsage = 0;
    }
};

