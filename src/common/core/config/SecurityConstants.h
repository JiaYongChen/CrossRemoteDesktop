#pragma once

#include <QtCore/QString>

namespace SecurityConstants {

// ---- 加密参数 ----
inline constexpr int AesKeyBits        = 256;
inline constexpr int AesKeyBytes        = 32;
inline constexpr int RsaKeyBits         = 2048;
inline constexpr int SaltSize           = 16;    // bytes
inline constexpr int IvSize             = 16;    // bytes
inline constexpr int HashIterations     = 10000;
inline constexpr int Pbkdf2Iterations   = 100000;
inline constexpr int Pbkdf2KeyLength    = 32;    // bytes
inline const QString DefaultCipherSuite = QStringLiteral("AES256-GCM-SHA384");
inline constexpr const char* DefaultCryptoUsername = "CrossRemoteDesktop";

// ---- 会话 ----
inline constexpr int SessionTimeoutMs   = 3600000;  // 1 hour

// ---- 认证速率限制 ----
inline constexpr int MaxAuthFailures    = 5;
inline constexpr int AuthBaseDelayMs    = 1000;
inline constexpr int AuthMaxDelayMs     = 30000;

} // namespace SecurityConstants
