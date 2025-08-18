#include "config.h"
#include "logging_categories.h"
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QMutexLocker>
#include <QtCore/QTimer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QFile>
#include <QtCore/QDebug>
#include <QtCore/QMetaProperty>
#include <QtCore/QMetaObject>
#include <QtCore/QMessageLogger>

// Static member definitions
Config *Config::s_instance = nullptr;
QMutex Config::s_instanceMutex;

Config::Config(QObject *parent)
    : QObject(parent)
    , m_settings(nullptr)
    , m_autoSave(true)
    , m_autoReload(true)
    , m_isLoaded(false)
    , m_isModified(false)
    , m_lastModified()
    , m_encrypted(false)
    , m_encryptionPassword()
    , m_fileWatcher(nullptr)
    , m_watchFileChanges(true)
    , m_groupStack()
    , m_currentArrayName()
    , m_currentArrayIndex(-1)
    , m_configVersion()
    , m_currentVersion()
{
    // 设置默认配置文件路径
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(appDataPath);
    m_configFilePath = appDataPath + "/config.ini";
    
    // 初始化文件监控
    if (m_autoReload) {
        m_fileWatcher = new QFileSystemWatcher(this);
        connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, &Config::onFileChanged);
    }
}

Config::~Config()
{
    if (m_autoSave && m_isModified) {
        save();
    }
}

Config* Config::instance()
{
    if (s_instance == nullptr) {
        QMutexLocker locker(&s_instanceMutex);
        if (s_instance == nullptr) {
            s_instance = new Config();
        }
    }
    return s_instance;
}

void Config::setConfigFile(const QString &filePath, ConfigFormat format)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_autoSave && m_isModified) {
        save();
    }
    
    m_configFilePath = filePath;
    m_configFormat = format;
    
    // 重建 QSettings 实例（目前实现仅支持 INI）
    if (m_settings) {
        delete m_settings;
        m_settings = nullptr;
    }
    QSettings::Format settingsFormat = QSettings::IniFormat;
    if (m_configFormat != IniFormat) {
        // 其它格式暂未实现，仍然回落到 INI，避免崩溃
        settingsFormat = QSettings::IniFormat;
    }
    m_settings = new QSettings(m_configFilePath, settingsFormat);
    
    // 文件监控
    if (m_fileWatcher) {
        m_fileWatcher->removePaths(m_fileWatcher->files());
        m_fileWatcher->addPath(m_configFilePath);
    }
}

QString Config::configFile() const
{
    QMutexLocker locker(&m_mutex);
    return m_configFilePath;
}

// setConfigFormat function removed - not declared in header
// configFormat function removed - not declared in header

bool Config::load()
{
    QMutexLocker locker(&m_mutex);
    
    QFile file(m_configFilePath);
    if (!file.exists()) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "Config file does not exist:" << m_configFilePath;
        return false;
    }
    
    if (!file.open(QIODevice::ReadOnly)) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcApp) << "Failed to open config file for reading:" << m_configFilePath;
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    // 解密数据（如果启用）
    if (m_encrypted) {
        data = decrypt(data);
        if (data.isEmpty()) {
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcApp) << "Failed to decrypt config file";
            return false;
        }
    }
    
    // 解压功能已简化
    
    bool success = false;
    
    switch (m_configFormat) {
    case ConfigFormat::IniFormat:
        success = loadIni();
        break;
    case ConfigFormat::JsonFormat:
        success = loadJson();
        break;
    case ConfigFormat::XmlFormat:
        success = loadXml();
        break;
    case ConfigFormat::BinaryFormat:
        // Binary format not implemented
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcApp) << "Binary format not supported";
        return false;
    }
    
    if (success) {
        m_isModified = false;
        emit configLoaded();
    }
    
    return success;
}

bool Config::save()
{
    QMutexLocker locker(&m_mutex);
    
    // 备份功能已简化
    
    bool success = false;
    switch (m_configFormat) {
    case ConfigFormat::IniFormat:
        success = saveIni();
        break;
    case ConfigFormat::JsonFormat:
        success = saveJson();
        break;
    case ConfigFormat::XmlFormat:
        success = saveXml();
        break;
    case ConfigFormat::BinaryFormat:
        // Binary format not implemented
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcApp) << "Binary format not supported";
        return false;
    }
    
    if (success) {
        m_isModified = false;
        emit configSaved();
    }

    return success;
}

void Config::setValue(const QString &key, const QVariant &value, ConfigGroup group)
{
    QMutexLocker locker(&m_mutex);
    
    QString groupKey = getGroupKey(key, group);
    
    if (m_settings && m_settings->value(groupKey) != value) {
        m_settings->setValue(groupKey, value);
        m_isModified = true;
        emit valueChanged(key, value, group);
    }
}

QVariant Config::value(const QString &key, const QVariant &defaultValue, ConfigGroup group) const
{
    QMutexLocker locker(&m_mutex);
    
    QString groupKey = getGroupKey(key, group);
    return m_settings ? m_settings->value(groupKey, defaultValue) : defaultValue;
}

bool Config::contains(const QString &key, ConfigGroup group) const
{
    QMutexLocker locker(&m_mutex);
    
    QString groupKey = getGroupKey(key, group);
    return m_settings ? m_settings->contains(groupKey) : false;
}

void Config::remove(const QString &key, ConfigGroup group)
{
    QMutexLocker locker(&m_mutex);
    
    QString groupKey = getGroupKey(key, group);
    
    if (m_settings && m_settings->contains(groupKey)) {
        m_settings->remove(groupKey);
        m_isModified = true;
        emit valueChanged(key, QVariant(), group);
    }
}

void Config::clear(ConfigGroup group)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_settings) {
        // Clear specific group or all if General
        if (group == General) {
            m_settings->clear();
        } else {
            QString groupName = groupToString(group);
            m_settings->beginGroup(groupName);
            m_settings->remove("");
            m_settings->endGroup();
        }
        m_isModified = true;
    }
}

QStringList Config::allKeys() const
{
    QMutexLocker locker(&m_mutex);
    return m_settings ? m_settings->allKeys() : QStringList();
}

QStringList Config::keys(ConfigGroup group) const
{
    QMutexLocker locker(&m_mutex);
    if (!m_settings) return QStringList();
    
    QString groupName = groupToString(group);
    m_settings->beginGroup(groupName);
    QStringList keys = m_settings->childKeys();
    m_settings->endGroup();
    return keys;
}

QStringList Config::childGroups() const
{
    QMutexLocker locker(&m_mutex);
    return m_settings ? m_settings->childGroups() : QStringList();
}

void Config::beginGroup(const QString &groupName)
{
    QMutexLocker locker(&m_mutex);
    
    m_groupStack.push_back(groupName);
    if (m_settings) {
        m_settings->beginGroup(groupName);
    }
}

void Config::endGroup()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_groupStack.isEmpty()) {
        m_groupStack.removeLast();
        if (m_settings) {
            m_settings->endGroup();
        }
    }
}

QString Config::currentGroup() const
{
    QMutexLocker locker(&m_mutex);
    return m_groupStack.isEmpty() ? QString() : m_groupStack.join("/");
}

bool Config::importFromFile(const QString &filePath, ConfigFormat format)
{
    QMutexLocker locker(&m_mutex);
    
    QSettings::Format settingsFormat = QSettings::IniFormat;
    if (format == JsonFormat) {
        // For JSON, we'll need to handle it differently
        return false; // Not implemented for JSON yet
    }
    
    QSettings importSettings(filePath, settingsFormat);
    
    QStringList keys = importSettings.allKeys();
    for (const QString &key : keys) {
        setValue(key, importSettings.value(key), General);
    }
    
    return true;
}

bool Config::exportToFile(const QString &filePath, ConfigFormat format) const
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_settings) return false;
    
    QSettings::Format settingsFormat = QSettings::IniFormat;
    if (format == JsonFormat) {
        // For JSON, we'll need to handle it differently
        return false; // Not implemented for JSON yet
    }
    
    QSettings exportSettings(filePath, settingsFormat);
    
    QStringList keys = m_settings->allKeys();
    
    for (const QString &key : keys) {
        exportSettings.setValue(key, m_settings->value(key));
    }
    
    exportSettings.sync();
    return exportSettings.status() == QSettings::NoError;
}

// createBackup function removed - not declared in header

// restoreFromBackup function removed - not declared in header

// loadDefaults function removed - m_defaultValues not available

// setDefault function removed - not declared in header

// getDefault function removed - not declared in header

// validate function removed - not declared in header

// addValidator function removed - not declared in header

// removeValidator function removed - not declared in header

// migrate function removed - not declared in header

// addMigrationHandler function removed - not declared in header

void Config::setEncryption(bool enabled, const QString &key)
{
    QMutexLocker locker(&m_mutex);
    m_encrypted = enabled;
    m_encryptionPassword = key;
}

// setCompression function removed - not declared in header

// setAutoSave function removed - not declared in header

void Config::setAutoReload(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_autoReload = enabled;
    
    if (enabled && m_fileWatcher) {
        // File watcher already setup in constructor
    } else if (!enabled && m_fileWatcher) {
        delete m_fileWatcher;
        m_fileWatcher = nullptr;
    }
}

bool Config::isModified() const
{
    QMutexLocker locker(&m_mutex);
    return m_isModified;
}

QDateTime Config::lastModified() const
{
    QMutexLocker locker(&m_mutex);
    QFileInfo info(m_configFilePath);
    return info.lastModified();
}

qint64 Config::fileSize() const
{
    QMutexLocker locker(&m_mutex);
    QFileInfo info(m_configFilePath);
    return info.size();
}



// onFileChanged function removed - not declared in header



// cleanupOldBackups function removed - not declared in header

// loadFromIni function removed - not declared in header

// loadFromJson function removed - not declared in header

// loadFromXml function removed - not declared in header



// saveToJson function removed - not declared in header

// saveToXml function removed - not declared in header

QByteArray Config::encrypt(const QByteArray &data) const
{
    if (m_encryptionPassword.isEmpty()) {
        return QByteArray();
    }
    
    // 简单的XOR加密（实际应用中应使用更强的加密算法）
    QByteArray key = m_encryptionPassword.toUtf8();
    QByteArray encrypted = data;
    
    for (int i = 0; i < encrypted.size(); ++i) {
        encrypted[i] = encrypted[i] ^ key[i % key.size()];
    }
    
    return encrypted;
}

QByteArray Config::decrypt(const QByteArray &data) const
{
    // XOR加密的解密与加密相同
    return encrypt(data);
}

bool Config::saveIni() const
{
    if (!m_settings) {
        return false;
    }
    
    m_settings->sync();
    return m_settings->status() == QSettings::NoError;
}

bool Config::saveJson() const
{
    // JSON format not fully implemented
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcApp) << "JSON format save not implemented";
    return false;
}

bool Config::saveXml() const
{
    // XML format not implemented
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcApp) << "XML format save not implemented";
    return false;
}

bool Config::loadIni()
{
    if (!m_settings) {
        return false;
    }
    
    m_settings->sync();
    return m_settings->status() == QSettings::NoError;
}

bool Config::loadJson()
{
    // JSON format not fully implemented
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcApp) << "JSON format load not implemented";
    return false;
}

bool Config::loadXml()
{
    // XML format not implemented
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcApp) << "XML format load not implemented";
    return false;
}

QString Config::getGroupKey(const QString &key, ConfigGroup group) const
{
    QString prefix;
    switch (group) {
    case ConfigGroup::General:
        prefix = "General/";
        break;
    case ConfigGroup::Connection:
        prefix = "Connection/";
        break;
    case ConfigGroup::Network:
        prefix = "Network/";
        break;
    case ConfigGroup::Display:
        prefix = "Display/";
        break;
    case ConfigGroup::Audio:
        prefix = "Audio/";
        break;
    case ConfigGroup::Security:
        prefix = "Security/";
        break;
    case ConfigGroup::Performance:
        prefix = "Performance/";
        break;
    case ConfigGroup::UI:
        prefix = "UI/";
        break;
    case ConfigGroup::Logging:
        prefix = "Logging/";
        break;
    case ConfigGroup::Advanced:
        prefix = "Advanced/";
        break;
    }
    
    // 添加当前组路径
    if (!m_groupStack.isEmpty()) {
        prefix += m_groupStack.join("/") + "/";
    }
    
    return prefix + key;
}

QString Config::groupToString(ConfigGroup group)
{
    switch (group) {
    case ConfigGroup::General:
        return "General";
    case ConfigGroup::Connection:
        return "Connection";
    case ConfigGroup::Network:
        return "Network";
    case ConfigGroup::Display:
        return "Display";
    case ConfigGroup::Audio:
        return "Audio";
    case ConfigGroup::Security:
        return "Security";
    case ConfigGroup::Performance:
        return "Performance";
    case ConfigGroup::UI:
        return "UI";
    case ConfigGroup::Logging:
        return "Logging";
    case ConfigGroup::Advanced:
        return "Advanced";
    }
    return "General";
}

void Config::onFileChanged(const QString &path)
{
    Q_UNUSED(path)
    // File change handling removed
}

void Config::saveIfModified()
{
    if (m_isModified) {
        save();
    }
}

// ConfigBinding template class removed - not declared in header