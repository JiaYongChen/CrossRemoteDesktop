#include "client/network/ServerTrustStore.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtNetwork/QHostAddress>

#include "common/config/SettingsManager.h"

namespace {
constexpr const char* kKeyEndpoint    = "endpoint";
constexpr const char* kKeyFingerprint = "fingerprint";
constexpr const char* kKeyFirstSeen   = "firstSeen";

/// 查找 endpoint 对应条目索引；非对象/缺 endpoint 的畸形条目自然不匹配（被跳过），未找到返回 -1
int findEndpoint(const QJsonArray& hosts, const QString& endpoint) {
    for (int i = 0; i < hosts.size(); ++i) {
        if (hosts.at(i).toObject().value(kKeyEndpoint).toString() == endpoint) {
            return i;
        }
    }
    return -1;
}
} // namespace

ServerTrustStore::ServerTrustStore(SettingsManager& settings)
    : m_settings(settings) {
}

QString ServerTrustStore::endpointFor(const QString& host, quint16 port) {
    const QHostAddress asIp(host);
    const QString normalized = asIp.isNull() ? host.toLower() : asIp.toString();
    return QStringLiteral("%1:%2").arg(normalized).arg(port);
}

ServerTrustStore::VerifyResult ServerTrustStore::verify(const QString& endpoint,
                                                        const QString& fingerprint) const {
    const QJsonArray hosts = m_settings.trustedHosts();
    const int idx = findEndpoint(hosts, endpoint);
    if (idx < 0) {
        return VerifyResult::FirstUse;
    }
    const QString stored = hosts.at(idx).toObject().value(kKeyFingerprint).toString();
    if ( stored.isEmpty() ) {
        return VerifyResult::FirstUse;   // 损坏条目（指纹缺失）按首连自愈重录，不误报 MITM
    }
    return (stored == fingerprint) ? VerifyResult::Trusted : VerifyResult::Changed;
}

void ServerTrustStore::recordTrust(const QString& endpoint, const QString& fingerprint) {
    QJsonArray hosts = m_settings.trustedHosts();
    const int idx = findEndpoint(hosts, endpoint);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QJsonObject entry;
    if (idx >= 0) {
        entry = hosts.at(idx).toObject();   // 保留 firstSeen
    } else {
        entry[kKeyEndpoint] = endpoint;
        entry[kKeyFirstSeen] = now;
    }
    entry[kKeyFingerprint] = fingerprint;

    if (idx >= 0) {
        hosts.replace(idx, entry);
    } else {
        hosts.append(entry);
    }
    m_settings.setTrustedHosts(hosts);
    // 信任记录是安全攸关状态（丢失 = 静默失去 MITM 变更检测）：同步写穿，
    // 不依赖去抖定时器（崩溃/强杀于去抖窗口内也不丢）
    m_settings.save();
}

QString ServerTrustStore::storedFingerprint(const QString& endpoint) const {
    const QJsonArray hosts = m_settings.trustedHosts();
    const int idx = findEndpoint(hosts, endpoint);
    if (idx < 0) {
        return {};
    }
    return hosts.at(idx).toObject().value(kKeyFingerprint).toString();
}
