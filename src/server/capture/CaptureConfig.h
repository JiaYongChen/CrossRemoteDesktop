#ifndef CAPTURECONFIG_H
#define CAPTURECONFIG_H

#include <QtCore/QRect>
#include <chrono>

/**
 * @brief 屏幕捕获配置结构体
 * 
 * 统一的配置结构，用于ScreenCapture和ScreenCaptureWorker之间的配置传递。
 * 避免重复定义，提供清晰的配置管理接口。
 */
struct CaptureConfig {
    int frameRate = 30;                    ///< 目标帧率
    double quality = 0.8;                 ///< 捕获质量 (0.0-1.0)
    bool highDefinition = true;            ///< 高清模式
    bool antiAliasing = true;              ///< 抗锯齿
    bool highScaleQuality = true;          ///< 高质量缩放
    QRect captureRect;                     ///< 捕获区域 (空表示全屏)
    int maxQueueSize = 10;                 ///< 最大队列大小
    
    /**
     * @brief 验证配置参数的有效性
     * @return 如果配置有效返回true，否则返回false
     */
    bool isValid() const {
        return frameRate > 0 && frameRate <= 120 &&
               quality >= 0.0 && quality <= 1.0 &&
               maxQueueSize > 0 && maxQueueSize <= 100;
    }
    
    /**
     * @brief 重置为默认配置
     */
    void reset() {
        frameRate = 30;
        quality = 0.8;
        highDefinition = true;
        antiAliasing = true;
        highScaleQuality = true;
        captureRect = QRect();
        maxQueueSize = 10;
    }
    
    /**
     * @brief 比较两个配置是否相等
     * @param other 另一个配置对象
     * @return 如果配置相等返回true，否则返回false
     */
    bool operator==(const CaptureConfig& other) const {
        return frameRate == other.frameRate &&
               qFuzzyCompare(quality, other.quality) &&
               highDefinition == other.highDefinition &&
               antiAliasing == other.antiAliasing &&
               highScaleQuality == other.highScaleQuality &&
               captureRect == other.captureRect &&
               maxQueueSize == other.maxQueueSize;
    }
    
    /**
     * @brief 比较两个配置是否不相等
     * @param other 另一个配置对象
     * @return 如果配置不相等返回true，否则返回false
     */
    bool operator!=(const CaptureConfig& other) const {
        return !(*this == other);
    }
};

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

#endif // CAPTURECONFIG_H