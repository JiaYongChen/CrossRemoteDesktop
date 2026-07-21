#pragma once

namespace ProtocolConstants {

// ---- 协议标识 ----
inline constexpr quint32 ProtocolMagic     = 0x52444350;   // "RDCP"
inline constexpr quint32 ProtocolVersion   = 1;
inline constexpr quint32 SerializedHeaderSize{5 * sizeof(quint32) + sizeof(quint64)}; // 28 bytes, brace init prevents narrowing

// ---- 消息字段长度上限 ----
inline constexpr quint32 MaxUsernameLength       = 256;
inline constexpr quint32 MaxPasswordHashLength   = 512;
inline constexpr quint32 MaxSessionIdLength      = 512;
inline constexpr quint32 MaxHostnameLength       = 1024;
inline constexpr quint32 MaxTextLength           = 64 * 1024;
inline constexpr quint32 MaxGenericStringLength  = 4096;

} // namespace ProtocolConstants
