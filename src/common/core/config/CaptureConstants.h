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

/// 验证帧率是否在有效范围内
inline constexpr bool IsValidFrameRate(int fps) {
    return fps >= MinFrameRate && fps <= MaxFrameRate;
}

} // namespace CaptureConstants
