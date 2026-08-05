#include "AppVersion.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QStringList>

namespace {

// 解析单个版本段：1~3 个字符且全部为 ASCII 数字（排除 +/-/空白/空段；3 位上限天然约束值域 ≤999）
bool parseSegment(const QString& segment, int& out) {
    if ( segment.isEmpty() || segment.size() > 3 ) {
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
    static const std::optional<AppVersion> localVersion =
        AppVersion::parse(QCoreApplication::applicationVersion());
    Q_ASSERT(localVersion.has_value());

    const std::optional<AppVersion> peer = AppVersion::parse(peerVersion);
    return peer.has_value() && localVersion.has_value() && *peer == *localVersion;
}
