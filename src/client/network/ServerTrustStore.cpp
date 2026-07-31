#include "client/network/ServerTrustStore.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

#include "common/config/SettingsManager.h"

namespace {
constexpr const char* kKeyEndpoint    = "endpoint";
constexpr const char* kKeyFingerprint = "fingerprint";
constexpr const char* kKeyFirstSeen   = "firstSeen";
constexpr const char* kKeyLastSeen    = "lastSeen";

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

ServerTrustStore::VerifyResult ServerTrustStore::verify(const QString& endpoint,
                                                        const QString& fingerprint) const {
    const QJsonArray hosts = m_settings.trustedHosts();
    const int idx = findEndpoint(hosts, endpoint);
    if (idx < 0) {
        return VerifyResult::FirstUse;
    }
    const QString stored = hosts.at(idx).toObject().value(kKeyFingerprint).toString();
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
    entry[kKeyLastSeen] = now;

    if (idx >= 0) {
        hosts.replace(idx, entry);
    } else {
        hosts.append(entry);
    }
    m_settings.setTrustedHosts(hosts);
}

QString ServerTrustStore::storedFingerprint(const QString& endpoint) const {
    const QJsonArray hosts = m_settings.trustedHosts();
    const int idx = findEndpoint(hosts, endpoint);
    if (idx < 0) {
        return {};
    }
    return hosts.at(idx).toObject().value(kKeyFingerprint).toString();
}
