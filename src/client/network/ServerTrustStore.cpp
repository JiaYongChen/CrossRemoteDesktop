#include "client/network/ServerTrustStore.h"

#include <optional>

#include <QtCore/QJsonObject>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QSsl>

#include "common/config/SettingsManager.h"
#include "common/logging/LoggingCategories.h"

namespace {
/// 顶层 JSON 键：endpoint → PEM 证书字符串 的映射对象
constexpr const char* kTrustedCertsKey = "trusted_certs";
} // namespace

ServerTrustStore::ServerTrustStore(SettingsManager& settings)
    : m_settings(settings) {
}

QString ServerTrustStore::endpointFor(const QString& host, quint16 port) {
    const QHostAddress asIp(host);
    const QString normalized = asIp.isNull() ? host.toLower() : asIp.toString();
    return QStringLiteral("%1:%2").arg(normalized).arg(port);
}

std::optional<QSslCertificate> ServerTrustStore::storedCertificate(const QString& endpoint) const {
    const QString pem = m_settings.value(kTrustedCertsKey).toJsonObject().value(endpoint).toString();
    if (pem.isEmpty()) {
        return std::nullopt;
    }
    const QSslCertificate cert(pem.toUtf8(), QSsl::Pem);
    if (cert.isNull()) {
        return std::nullopt;
    }
    return cert;
}

void ServerTrustStore::recordTrust(const QString& endpoint, const QSslCertificate& cert) {
    QJsonObject certs = m_settings.value(kTrustedCertsKey).toJsonObject();
    certs.insert(endpoint, QString::fromLatin1(cert.toPem()));
    m_settings.setValue(kTrustedCertsKey, certs);
    // 信任记录是安全攸关状态（丢失 = 静默失去 MITM 变更检测）：同步写穿，
    // 不依赖去抖定时器（崩溃/强杀于去抖窗口内也不丢）；写失败显性化
    if (!m_settings.save()) {
        qCWarning(lcClient) << "TOFU: 信任记录写穿失败，记录暂仅存内存（去抖/析构保存将重试）";
    }
}
