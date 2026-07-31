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

    /// 纯查询、无副作用
    [[nodiscard]] VerifyResult verify(const QString& endpoint, const QString& fingerprint) const;
    /// 写入或覆盖信任（首连记录 / 用户确认变更后更新），维护 firstSeen/lastSeen
    void recordTrust(const QString& endpoint, const QString& fingerprint);
    /// 取既有指纹（变更对话框展示"旧指纹"），无记录返回空串
    [[nodiscard]] QString storedFingerprint(const QString& endpoint) const;

private:
    SettingsManager& m_settings;
};
