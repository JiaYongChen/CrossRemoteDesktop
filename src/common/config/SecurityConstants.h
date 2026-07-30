#pragma once

#include <QtCore/QtGlobal>

namespace SecurityConstants {

// ---- 加密参数 ----
inline constexpr int IvSize             = 16;    // bytes
inline constexpr char DefaultCryptoUsername[] = "CrossRemoteDesktop";  // 数组类型避免指针退化

// ---- 认证速率限制 ----
inline constexpr int MaxAuthFailures    = 5;
inline constexpr int AuthBaseDelayMs    = 1000;
inline constexpr int AuthMaxDelayMs     = 30000;

// ---- PBKDF2 参数上限（防恶意服务端下发超大值致客户端资源耗尽）----
inline constexpr quint32 MaxPbkdf2Iterations = 10000000;  // 服务端实际用 100000，上限留余量
inline constexpr quint32 MaxPbkdf2KeyLength  = 1024;      // 派生密钥字节上限（服务端实际用 32）

} // namespace SecurityConstants
