#pragma once

#include <optional>

#include <QtCore/QString>
#include <QtNetwork/QSslCertificate>

class SettingsManager;

/**
 * @brief TOFU 服务端身份信任库
 *
 * 封装"查/记"与 JSON 结构，持久化委托给注入的 SettingsManager。
 * 存储格式：JSON 顶层键 "trusted_certs" 下为 endpoint → PEM 证书字符串 的映射对象。
 * 纯客户端关切，UI 无关，可脱离网络独立单测。
 */
class ServerTrustStore {
public:
    explicit ServerTrustStore(SettingsManager& settings);

    /// 归一化的信任库键：IP 字面量经 QHostAddress 规范化，主机名小写化，拼接 ":port"。
    /// 同一服务端不因输入拼写差异（大小写/IP 文本形式）裂成多个条目——
    /// 裂键会让"换个拼写连接"命中首连路径静默信任新证书，绕过变更检测
    [[nodiscard]] static QString endpointFor(const QString& host, quint16 port);

    /// 纯查询、无副作用：读取 endpoint 对应的 PEM 并解析为证书。
    /// 无记录或记录损坏（PEM 空/不可解析）返回 std::nullopt
    [[nodiscard]] std::optional<QSslCertificate> storedCertificate(const QString& endpoint) const;
    /// 写入或覆盖信任（首连记录 / 用户确认变更后更新）；同步写穿落盘
    void recordTrust(const QString& endpoint, const QSslCertificate& cert);

private:
    SettingsManager& m_settings;
};
