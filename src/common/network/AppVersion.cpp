#include "AppVersion.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QStringList>

namespace {

// 解析单个版本段：1~3 个字符且全部为 ASCII 数字，拒绝前导零（"01"/"00" 非法，"0" 合法）
bool parseSegment(const QString& segment, int& out) {
    if ( segment.isEmpty() || segment.size() > 3 ) {
        return false;
    }
    // 前导零拒绝：段长 >1 且首字符为 '0'（防止 "01"→1 绕过严格相等判定）
    if ( segment.size() > 1 && segment.at(0) == QLatin1Char('0') ) {
        return false;
    }
    int value = 0;
    for ( const QChar& ch : segment ) {
        const char16_t c = ch.unicode();
        if ( c < u'0' || c > u'9' ) {
            return false;
        }
        value = value * 10 + (c - u'0');
    }
    out = value;
    return true;
}

} // namespace

std::optional<AppVersion> AppVersion::parse(const QString& text) {
    const QStringList segments = text.split('.');
    if ( segments.size() != 3 ) {
        return std::nullopt;
    }
    AppVersion version;
    if ( !parseSegment(segments.at(0), version.major)
         || !parseSegment(segments.at(1), version.minor)
         || !parseSegment(segments.at(2), version.patch) ) {
        return std::nullopt;
    }
    return version;
}

bool appVersionMatches(const QString& peerVersion) {
    // 每次调用即时读取 applicationVersion，不缓存——避免 static 局部变量
    // 在 setApplicationVersion 之前被首次调用时永久锁定为空串
    const std::optional<AppVersion> localVersion =
        AppVersion::parse(QCoreApplication::applicationVersion());
    if ( !localVersion.has_value() ) {
        return false;   // 本机版本未设置或无效：fail-closed 拒绝一切握手
    }

    const std::optional<AppVersion> peer = AppVersion::parse(peerVersion);
    return peer.has_value() && *peer == *localVersion;
}
