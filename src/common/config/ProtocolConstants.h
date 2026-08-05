#pragma once

#include <QtCore/QtGlobal>

namespace ProtocolConstants {

// ---- 协议标识 ----
inline constexpr quint32 ProtocolMagic     = 0x52444350;   // "RDCP"
inline constexpr quint32 SerializedHeaderSize{4 * sizeof(quint32) + sizeof(quint64)}; // 24 bytes, brace init prevents narrowing

// ---- 消息字段长度上限 ----
inline constexpr quint32 MaxAppVersionLength     = 32;  // 版本字符串线上长度上限
inline constexpr quint32 MaxUsernameLength       = 256; // 用户名长度上限（UTF-8 字节数）
inline constexpr quint32 MaxPasswordHashLength   = 512; // 密码哈希长度上限
inline constexpr quint32 MaxSessionIdLength      = 512; // 会话 ID 长度上限
inline constexpr quint32 MaxHostnameLength       = 1024; // 主机名长度上限（UTF-8 字节数）
inline constexpr quint32 MaxTextLength           = 64 * 1024; // 文本剪贴板长度上限（UTF-8 字节数）
inline constexpr quint32 MaxGenericStringLength  = 4096; // 通用字符串长度上限（UTF-8 字节数）
inline constexpr quint32 MaxClipboardPayloadSize = 10 * 1024 * 1024;  // 剪贴板消息载荷上限 10MB

} // namespace ProtocolConstants
