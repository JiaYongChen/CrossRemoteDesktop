#pragma once

#include <QtCore/qglobal.h>

namespace GuiConstants {

// ---- 窗口尺寸 ----
inline constexpr int MinWindowWidth     = 800;
inline constexpr int MinWindowHeight    = 600;
inline constexpr int MainWindowWidth    = 1200;
inline constexpr int MainWindowHeight   = 800;

// ---- UI 统计 ----
inline constexpr int UiStatsUpdateIntervalMs = 500;   // 0.5s（UI 刷新快于管线统计）

// ---- OpenGL 渲染 ----
inline constexpr int PboCount              = 2;
inline constexpr int TexCount              = 2;
inline constexpr int RgbChannels           = 3;
inline constexpr quint64 MetricsReportInterval = 10;   // frames
inline constexpr double FpsAlpha           = 0.1;       // EMA 平滑系数

// ---- 帧丢弃策略 ----
enum class FrameDropPolicy {
    KeepOldest,   // 队列满时丢弃新帧
    KeepLatest,   // 队列满时丢弃最旧帧
    LatestOnly,   // 容量=1，始终只保留最新帧（最低延迟）
};

// ---- 客户端/GL 默认配置 ----
inline constexpr FrameDropPolicy DefaultFrameDropPolicy = FrameDropPolicy::LatestOnly;
inline constexpr int DefaultQueueCapacity  = 5;
inline constexpr bool DefaultVsyncEnabled  = true;
inline constexpr bool DefaultUsePbo        = true;
inline constexpr bool DefaultUsePersistentPbo = true;

} // namespace GuiConstants
