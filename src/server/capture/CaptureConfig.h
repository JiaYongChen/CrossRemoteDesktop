#pragma once

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
    bool highDefinition = true;            ///< 高清模式
    bool antiAliasing = true;              ///< 抗锯齿
    bool highScaleQuality = true;          ///< 高质量缩放
    QRect captureRect;                     ///< 捕获区域 (空表示全屏)
    int maxQueueSize = 10;                 ///< 最大队列大小
    
};

/**
 * @brief 捕获统计信息结构体
 */
struct CaptureStats {
    quint64 totalFramesCaptured = 0;       ///< 总捕获帧数
};

