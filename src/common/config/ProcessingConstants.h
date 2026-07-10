#pragma once

namespace ProcessingConstants {

// ---- 线程与并发 ----
inline constexpr int DefaultThreadPoolSize        = 4;
inline constexpr int MaxSendBatch                 = 6;
inline constexpr int MaxInFlightBatches           = 1;

// ---- 缓冲区 ----
inline constexpr int StreamBufferSize = 32 * 1024;    // 32 KB
inline constexpr int ImageBufferSize  = 512 * 1024;   // 512 KB

// ---- 队列控制 ----
inline constexpr int MaxQueueSize           = 1;    // Drain-to-Latest
inline constexpr int QueueWarningThreshold  = 80;   // % 容量
inline constexpr int QueueErrorThreshold    = 95;   // % 容量

// ---- JPEG 压缩质量 ----
inline constexpr int JpegQualityHigh      = 85;
inline constexpr int JpegQualityMedium    = 70;
inline constexpr int JpegQualityLow       = 50;
inline constexpr int JpegQualityMin       = 30;
inline constexpr int DefaultJpegQuality   = 85;

// ---- 缩放因子 ----
inline constexpr double ScaleFactorHigh   = 1.0;
inline constexpr double ScaleFactorMedium = 0.75;
inline constexpr double ScaleFactorLow    = 0.5;

// ---- 统计与监控 ----
inline constexpr int StatsUpdateIntervalMs       = 1000;   // 1s
inline constexpr int MemoryWarningThresholdMb    = 512;
inline constexpr int CpuUsageThresholdPercent    = 80;
inline constexpr int GarbageCollectionIntervalMs = 30000;  // 30s

// ---- 数据处理阈值 ----
inline constexpr double MaxProcessingLatencyMs  = 100.0;
inline constexpr double MinProcessingRateFps    = 10.0;
inline constexpr int DefaultProcessingTimeoutMs = 5000;

} // namespace ProcessingConstants
