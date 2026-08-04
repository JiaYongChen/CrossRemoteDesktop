#pragma once

#include <QtCore/QtGlobal>

namespace ProtocolConstants {

// ---- 协议标识 ----
inline constexpr quint32 ProtocolMagic     = 0x52444350;   // "RDCP"
inline constexpr char AppVersion[] = "1.0.0";       // 应用版本（握手版本认证的唯一依据）
inline constexpr quint32 MaxAppVersionLength = 32;  // 版本字符串线上长度上限
inline constexpr quint32 SerializedHeaderSize{4 * sizeof(quint32) + sizeof(quint64)}; // 24 bytes, brace init prevents narrowing

// ---- 消息字段长度上限 ----
inline constexpr quint32 MaxUsernameLength       = 256;
inline constexpr quint32 MaxPasswordHashLength   = 512;
inline constexpr quint32 MaxSessionIdLength      = 512;
inline constexpr quint32 MaxHostnameLength       = 1024;
inline constexpr quint32 MaxTextLength           = 64 * 1024;
inline constexpr quint32 MaxGenericStringLength  = 4096;
inline constexpr quint32 MaxClipboardPayloadSize = 10 * 1024 * 1024;  // 剪贴板消息载荷上限 10MB

} // namespace ProtocolConstants
