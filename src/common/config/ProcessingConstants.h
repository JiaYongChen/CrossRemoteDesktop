#pragma once

namespace ProcessingConstants {

// ---- 线程与并发 ----
inline constexpr int MaxSendBatch                 = 6;
inline constexpr int MaxInFlightBatches           = 1;

// ---- 队列控制 ----
inline constexpr int MaxQueueSize           = 1;    // Drain-to-Latest
inline constexpr int QueueWarningThreshold  = 80;   // % 容量
inline constexpr int QueueErrorThreshold    = 95;   // % 容量

// ---- JPEG 色度子采样 ----
/// 默认 JPEG 色度子采样模式：4:2:0（对应 TJSAMP_420 = 2）
/// 值必须与 turbojpeg.h 中 TJSAMP_* 枚举保持一致
inline constexpr int DefaultChromaSubsampling = 2;  // TJSAMP_420

// ---- JPEG 压缩质量 ----
inline constexpr int DefaultJpegQuality   = 85;

// ---- 缩放因子 ----
inline constexpr double ScaleFactorHigh   = 1.0;

// ---- 统计与监控 ----
inline constexpr int StatsUpdateIntervalMs       = 1000;   // 1s

// ---- 数据处理阈值 ----
inline constexpr double MaxProcessingLatencyMs  = 100.0;
inline constexpr double MinProcessingRateFps    = 10.0;
inline constexpr int DefaultProcessingTimeoutMs = 5000;

} // namespace ProcessingConstants
