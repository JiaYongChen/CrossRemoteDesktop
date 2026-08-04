#pragma once

#include <optional>

#include <QtCore/QString>

// 应用版本值类型：严格解析 "x.y.z"，用于握手版本认证（完全相等判定）
struct AppVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;

    // 严格解析：恰好三段、每段 1~3 个 ASCII 数字；任何违规返回 nullopt
    [[nodiscard]] static std::optional<AppVersion> parse(const QString& text);

    friend bool operator==(const AppVersion& lhs, const AppVersion& rhs) = default;
};

// 对端版本串可解析且与本机 ProtocolConstants::AppVersion 完全相等
[[nodiscard]] bool appVersionMatches(const QString& peerVersion);
