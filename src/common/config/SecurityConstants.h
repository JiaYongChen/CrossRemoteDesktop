#pragma once

namespace SecurityConstants {

// ---- 加密参数 ----
inline constexpr int IvSize             = 16;    // bytes
inline constexpr char DefaultCryptoUsername[] = "CrossRemoteDesktop";  // 数组类型避免指针退化

// ---- 认证速率限制 ----
inline constexpr int MaxAuthFailures    = 5;
inline constexpr int AuthBaseDelayMs    = 1000;
inline constexpr int AuthMaxDelayMs     = 30000;

} // namespace SecurityConstants
