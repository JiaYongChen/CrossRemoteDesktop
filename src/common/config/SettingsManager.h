#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVariant>

/**
 * @brief 应用程序设置管理器（JSON 文件持久化，DI 注入）
 *
 * 统一所有平台的配置文件格式为 JSON，存放于可执行文件同目录。
 * 构造时可选指定文件路径（默认 config.json），调用 load() 加载
 * 或自动从旧 QSettings 迁移数据。
 */
class SettingsManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @param filePath JSON 配置文件路径，为空则使用 applicationDirPath()/config.json
     * @param parent   父 QObject
     */
    explicit SettingsManager(const QString &filePath = QString(),
                             QObject *parent = nullptr);
    ~SettingsManager() override;

    void remove(const QString &key);

    // ---- 便捷类型方法 ----

    void setString(const QString &key, const QString &value);
    QString getString(const QString &key, const QString &defaultValue = {}) const;

    void setInt(const QString &key, int value);
    int getInt(const QString &key, int defaultValue = 0) const;

    void setBool(const QString &key, bool value);
    bool getBool(const QString &key, bool defaultValue = false) const;

    // ---- 连接历史（结构化存取） ----

    QJsonArray connectionHistory() const;
    void setConnectionHistory(const QJsonArray &entries);

    // ---- 受信任主机（TOFU 服务端证书指纹）----

    QJsonArray trustedHosts() const;
    void setTrustedHosts(const QJsonArray &entries);

    /**
     * @brief 从旧 QSettings 读取连接历史并转换为规范化 JSON 数组
     *
     * 迁移辅助函数：解析旧格式的并行 StringList，做类型转换与条目校验，
     * 委托 ConnectionHistory 完成解密/默认值/规范化。公开为 static 以便单元测试
     * 通过 INI 格式 QSettings 注入旧数据。
     */
    [[nodiscard]] static QJsonArray readLegacyConnectionHistory(QSettings &oldSettings);

    // ---- 文件操作 ----

    bool load();
    bool save();

    // ---- 基本存取（内部实现，外部通过便捷类型方法访问） ----

    void setValue(const QString &key, const QVariant &value);
    QVariant value(const QString &key, const QVariant &defaultValue = {}) const;

private:
    /// 从旧 QSettings 迁移全部配置到 m_root，返回是否存在旧数据被迁移
    [[nodiscard]] bool migrateFromQSettings();
    /// 销毁旧 QSettings 存储（仅在迁移数据成功落盘后调用）
    void clearLegacyQSettings();
    void scheduleSave();
    bool saveLocked();

    QJsonObject m_root;
    QString     m_filePath;
    bool        m_isModified = false;
    // 迁移数据尚未落盘时为 true，saveLocked() 成功后执行旧数据清理并复位
    bool        m_pendingLegacyCleanup = false;
    mutable QMutex m_mutex;

    // 去抖定时器：每次 setValue 后启动 500ms，到期执行 save()
    QTimer *m_saveTimer = nullptr;
};
