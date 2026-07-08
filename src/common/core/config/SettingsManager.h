#pragma once

#include <QtCore/QObject>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QVariant>
#include <QtCore/QString>
#include <QtCore/QMutex>

class QTimer;

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

    // ---- 基本存取 ----

    void setValue(const QString &key, const QVariant &value);
    QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
    bool contains(const QString &key) const;
    void remove(const QString &key);

    /** 获取 JSON 中某一分组下的所有键 */
    QStringList childKeys(const QString &group) const;

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

    // ---- 文件操作 ----

    bool load();
    bool save();
    QString filePath() const;
    bool isModified() const;

signals:
    void valueChanged(const QString &key, const QVariant &value);
    void saved();

private:
    void migrateFromQSettings();
    void scheduleSave();

    QJsonObject m_root;
    QString     m_filePath;
    bool        m_isModified = false;
    bool        m_migrationDone = false;
    mutable QMutex m_mutex;

    // 去抖定时器：每次 setValue 后启动 500ms，到期执行 save()
    QTimer *m_saveTimer = nullptr;
};
