#pragma once

#include <QtCore/QObject>
#include <QtCore/QSettings>
#include <QtCore/QVariant>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QSize>
#include "error/RdError.h"
#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonDocument>
#include <QtCore/QMutex>
#include <QtCore/QFileSystemWatcher>

class Config : public QObject
{
    Q_OBJECT
    
public:
    // 配置组
    enum ConfigGroup {
        General,
        Connection,
        Display,
        Security,
        Network,
        Performance,
        UI,
        Logging,
        Advanced
    };
    Q_ENUM(ConfigGroup)
    
    // 配置格式
    enum ConfigFormat {
        IniFormat,
        JsonFormat,
        XmlFormat,
        BinaryFormat
    };
    
    // 单例模式
    static Config* instance();
    static void destroyInstance();
    
    // 配置文件管理
    void setConfigFile(const QString &filePath, ConfigFormat format = IniFormat);
    QString configFile() const;
    ConfigFormat configFormat() const;
    
    void setAutoSave(bool enabled);
    bool autoSave() const;
    
    void setAutoReload(bool enabled);
    bool autoReload() const;
    
    // 基本操作
    void setValue(const QString &key, const QVariant &value, ConfigGroup group = General);
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant(), ConfigGroup group = General) const;
    
    bool contains(const QString &key, ConfigGroup group = General) const;
    void remove(const QString &key, ConfigGroup group = General);
    void clear(ConfigGroup group = General);
    void clearAll();
    
    QStringList keys(ConfigGroup group = General) const;
    QStringList allKeys() const;
    
    // 类型安全的getter/setter
    void setString(const QString &key, const QString &value, ConfigGroup group = General);
    QString getString(const QString &key, const QString &defaultValue = QString(), ConfigGroup group = General) const;
    
    void setInt(const QString &key, int value, ConfigGroup group = General);
    int getInt(const QString &key, int defaultValue = 0, ConfigGroup group = General) const;
    
    void setBool(const QString &key, bool value, ConfigGroup group = General);
    bool getBool(const QString &key, bool defaultValue = false, ConfigGroup group = General) const;
    
    void setDouble(const QString &key, double value, ConfigGroup group = General);
    double getDouble(const QString &key, double defaultValue = 0.0, ConfigGroup group = General) const;
    
    void setSize(const QString &key, const QSize &value, ConfigGroup group = General);
    QSize getSize(const QString &key, const QSize &defaultValue = QSize(), ConfigGroup group = General) const;
    
    void setPoint(const QString &key, const QPoint &value, ConfigGroup group = General);
    QPoint getPoint(const QString &key, const QPoint &defaultValue = QPoint(), ConfigGroup group = General) const;
    
    void setRect(const QString &key, const QRect &value, ConfigGroup group = General);
    QRect getRect(const QString &key, const QRect &defaultValue = QRect(), ConfigGroup group = General) const;
    
    void setColor(const QString &key, const QColor &value, ConfigGroup group = General);
    QColor getColor(const QString &key, const QColor &defaultValue = QColor(), ConfigGroup group = General) const;
    
    void setFont(const QString &key, const QFont &value, ConfigGroup group = General);
    QFont getFont(const QString &key, const QFont &defaultValue = QFont(), ConfigGroup group = General) const;
    
    void setByteArray(const QString &key, const QByteArray &value, ConfigGroup group = General);
    QByteArray getByteArray(const QString &key, const QByteArray &defaultValue = QByteArray(), ConfigGroup group = General) const;
    
    void setStringList(const QString &key, const QStringList &value, ConfigGroup group = General);
    QStringList getStringList(const QString &key, const QStringList &defaultValue = QStringList(), ConfigGroup group = General) const;
    
    // JSON支持
    void setJsonObject(const QString &key, const QJsonObject &value, ConfigGroup group = General);
    QJsonObject getJsonObject(const QString &key, const QJsonObject &defaultValue = QJsonObject(), ConfigGroup group = General) const;
    
    // 数组支持
    void beginArray(const QString &arrayName, ConfigGroup group = General);
    void endArray();
    void setArrayIndex(int index);
    int arraySize(const QString &arrayName, ConfigGroup group = General) const;
    void removeArrayIndex(const QString &arrayName, int index, ConfigGroup group = General);
    
    // 组操作
    void beginGroup(const QString &groupName);
    void endGroup();
    QString currentGroup() const;
    QStringList childGroups() const;
    
    // 文件操作
    bool load();
    bool save();
    bool reload();
    
    bool exportToFile(const QString &filePath, ConfigFormat format = IniFormat) const;
    bool importFromFile(const QString &filePath, ConfigFormat format = IniFormat);
    
    // 备份和恢复
    bool backup(const QString &backupPath = QString()) const;
    bool restore(const QString &backupPath);
    QStringList availableBackups() const;
    
    // 默认配置
    void loadDefaults();
    void resetToDefaults();
    bool isDefault(const QString &key, ConfigGroup group = General) const;
    
    // 配置验证
    bool validate() const;
    QStringList validationErrors() const;
    
    // 配置迁移
    void setConfigVersion(const QString &version);
    QString configVersion() const;
    bool needsMigration() const;
    bool migrate();
    
    // 监控
    void setWatchFileChanges(bool enabled);
    bool watchFileChanges() const;
    
    // 状态
    bool isLoaded() const;
    bool isModified() const;
    QDateTime lastModified() const;
    qint64 fileSize() const;
    
    // 工具函数
    static QString groupToString(ConfigGroup group);
    static ConfigGroup stringToGroup(const QString &groupStr);
    static QString formatToString(ConfigFormat format);
    static ConfigFormat stringToFormat(const QString &formatStr);
    
    // 路径工具
    static QString defaultConfigPath();
    static QString userConfigPath();
    static QString systemConfigPath();
    
signals:
    void valueChanged(const QString &key, const QVariant &value, ConfigGroup group);
    void configLoaded();
    void configSaved();
    void configReloaded();
    void fileChanged(const QString &filePath);
    void errorOccurred(const RdError &error);
    
public slots:
    void onFileChanged(const QString &filePath);
    void saveIfModified();
    
protected:
    explicit Config(QObject *parent = nullptr);
    ~Config() override;
    
private:
    // 内部结构
    struct ConfigEntry {
        QVariant value;
        QVariant defaultValue;
        bool isModified;
        QString description;
    };
    
    // 格式处理
    bool loadIni();
    bool saveIni() const;
    bool loadJson();
    bool saveJson() const;
    bool loadXml();
    bool saveXml() const;
    bool loadBinary();
    bool saveBinary() const;
    
    // 路径处理
    QString getGroupKey(const QString &key, ConfigGroup group) const;
    QString getFullKey(const QString &key) const;
    
    // 默认值设置
    void setupDefaults();
    void setDefaultValue(const QString &key, const QVariant &value, ConfigGroup group, const QString &description = QString());
    
    // 验证
    bool validateKey(const QString &key, const QVariant &value, ConfigGroup group) const;
    
    // 迁移
    void migrateFromVersion(const QString &fromVersion);
    
    // 成员变量
    static Config *s_instance;
    static QMutex s_instanceMutex;
    
    QString m_configFilePath;
    ConfigFormat m_configFormat;
    QSettings *m_settings;
    
    QHash<QString, ConfigEntry> m_config;
    QHash<ConfigGroup, QHash<QString, ConfigEntry>> m_groupedConfig;
    
    bool m_autoSave;
    bool m_autoReload;
    bool m_isLoaded;
    bool m_isModified;
    QDateTime m_lastModified;
    
    // 监控
    QFileSystemWatcher *m_fileWatcher;
    bool m_watchFileChanges;
    
    // 数组和组状态
    QStringList m_groupStack;
    QString m_currentArrayName;
    int m_currentArrayIndex;
    
    // 默认配置
    QHash<QString, ConfigEntry> m_defaults;
    
    // 版本和迁移
    QString m_configVersion;
    QString m_currentVersion;
    
    // 验证错误
    mutable QStringList m_validationErrors;
    
    // 线程安全
    mutable QMutex m_mutex;
};

// ConfigBinding<T> 模板类及 CONFIG_* 便利宏已移除（零引用死代码）。
// 直接使用 Config::instance()->value() / setValue() 系列方法替代。

