#pragma once

namespace CaptureConstants {

// ---- 捕获帧率 ----
inline constexpr int DefaultFrameRate       = 60;       // fps
inline constexpr int MinFrameRate           = 1;
inline constexpr int MaxFrameRate           = 120;

// ---- 捕获内部阈值 ----
inline constexpr int CursorSampleIntervalMs    = 8;      // ~125Hz
inline constexpr int MaxCaptureTimeHistory     = 100;
inline constexpr int MaxFrameTimestampHistory  = 60;
inline constexpr int MaxErrorCount             = 10;
inline constexpr int MaxDxgiReinitAttempts     = 3;

} // namespace CaptureConstants
