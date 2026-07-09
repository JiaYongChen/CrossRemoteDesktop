#pragma once

namespace CaptureConstants {

// ---- 捕获帧率 ----
inline constexpr int DefaultFrameRate       = 60;       // fps
inline constexpr int MinFrameRate           = 1;
inline constexpr int MaxFrameRate           = 120;
inline constexpr int MillisecondsPerSecond  = 1000;

// ---- 日志与调试 ----
inline constexpr int DebugLogIntervalMs     = 1000;
inline constexpr int FailureLogIntervalMs   = 5000;

// ---- 捕获内部阈值 ----
inline constexpr int CursorSampleIntervalMs    = 8;      // ~125Hz
inline constexpr int MaxCaptureTimeHistory     = 100;
inline constexpr int MaxFrameTimestampHistory  = 60;
inline constexpr int MaxErrorCount             = 10;
inline constexpr int MaxDxgiReinitAttempts     = 3;

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

// ---- 输入处理 ----
inline constexpr int DefaultMouseSpeed    = 5;       // px/step
inline constexpr int DefaultKeyboardDelay = 50;      // ms
inline constexpr int DefaultMouseDelay    = 10;      // ms
inline constexpr int MaxKeyValue          = 0x01FFFFFF; // Qt::Key 最大值

/// 验证帧率是否在有效范围内
inline bool IsValidFrameRate(int fps) {
    return fps >= MinFrameRate && fps <= MaxFrameRate;
}

} // namespace CaptureConstants
