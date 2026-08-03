#pragma once

#include <QtCore/QString>

class SettingsManager;

/**
 * @brief TOFU 服务端身份信任库
 *
 * 封装"查/记/改"与 JSON 结构，持久化委托给注入的 SettingsManager。
 * 纯客户端关切，UI 无关、QtNetwork 无关，可脱离网络独立单测。
 */
class ServerTrustStore {
public:
    enum class VerifyResult {
        FirstUse,   ///< 无记录——首次连接
        Trusted,    ///< 记录存在且指纹匹配
        Changed     ///< 记录存在但指纹不符——疑似 MITM 或服务端重装
    };

    explicit ServerTrustStore(SettingsManager& settings);

    /// 归一化的信任库键：IP 字面量经 QHostAddress 规范化，主机名小写化，拼接 ":port"。
    /// 同一服务端不因输入拼写差异（大小写/IP 文本形式）裂成多个条目——
    /// 裂键会让"换个拼写连接"命中 FirstUse 静默信任新证书，绕过变更检测
    [[nodiscard]] static QString endpointFor(const QString& host, quint16 port);

    /// 纯查询、无副作用（条目存在但指纹为空的损坏条目按 FirstUse 自愈）
    [[nodiscard]] VerifyResult verify(const QString& endpoint, const QString& fingerprint) const;
    /// 写入或覆盖信任（首连记录 / 用户确认变更后更新），维护 firstSeen；同步写穿落盘
    void recordTrust(const QString& endpoint, const QString& fingerprint);
    /// 取既有指纹（变更对话框展示"旧指纹"），无记录返回空串
    [[nodiscard]] QString storedFingerprint(const QString& endpoint) const;

private:
    SettingsManager& m_settings;
};
