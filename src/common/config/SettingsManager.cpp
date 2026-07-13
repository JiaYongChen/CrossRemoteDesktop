#include "SettingsManager.h"
#include "../logging/LoggingCategories.h"
#include "../crypto/PasswordCrypto.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QMutexLocker>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>

// ============================================================
// 构造 / 析构
// ============================================================

SettingsManager::SettingsManager(const QString &filePath, QObject *parent)
    : QObject(parent)
{
    if (filePath.isEmpty()) {
        m_filePath = QCoreApplication::applicationDirPath() + "/config.json";
    } else {
        m_filePath = filePath;
    }

    // 去抖保存定时器（500ms）
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, &SettingsManager::save);
}

SettingsManager::~SettingsManager()
{
    if (m_isModified) {
        save();
    }
}

// ============================================================
// 文件路径
// ============================================================

QString SettingsManager::filePath() const
{
    QMutexLocker locker(&m_mutex);
    return m_filePath;
}

bool SettingsManager::isModified() const
{
    QMutexLocker locker(&m_mutex);
    return m_isModified;
}

// ============================================================
// 加载 / 保存
// ============================================================

bool SettingsManager::load()
{
    QMutexLocker locker(&m_mutex);

    QFileInfo fi(m_filePath);
    if (!fi.exists()) {
        // JSON 不存在 → 尝试从旧 QSettings 迁移
        migrateFromQSettings();
        m_isModified = true;
        saveLocked();  // 调用者已持锁，用内部版本避免死锁
        return true;
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcCoreConfig) << "SettingsManager: Cannot open config file for reading:"
                                << m_filePath;
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcCoreConfig) << "SettingsManager: JSON parse error:"
                                << parseError.errorString();
        return false;
    }

    if (!doc.isObject()) {
        qCWarning(lcCoreConfig) << "SettingsManager: JSON root is not an object";
        return false;
    }

    m_root = doc.object();
    m_isModified = false;
    qCInfo(lcCoreConfig) << "SettingsManager: Loaded config from" << m_filePath;
    return true;
}

bool SettingsManager::save()
{
    QMutexLocker locker(&m_mutex);
    return saveLocked();
}

bool SettingsManager::saveLocked()
{
    // 确保目标目录存在（调用者已持有 m_mutex）
    QFileInfo fi(m_filePath);
    QDir().mkpath(fi.absolutePath());

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcCoreConfig) << "SettingsManager: Cannot open config file for writing:"
                                << m_filePath;
        return false;
    }

    QJsonDocument doc(m_root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    m_isModified = false;
    emit saved();
    return true;
}

// ============================================================
// 基本存取（支持 "Group/key" 两层路径）
// ============================================================

void SettingsManager::setValue(const QString &key, const QVariant &value)
{
    QMutexLocker locker(&m_mutex);

    QStringList parts = key.split('/');
    if (parts.size() < 2) {
        m_root[key] = QJsonValue::fromVariant(value);
    } else {
        QJsonObject group = m_root.value(parts[0]).toObject();
        group[parts[1]] = QJsonValue::fromVariant(value);
        m_root[parts[0]] = group;
    }

    m_isModified = true;
    emit valueChanged(key, value);
    scheduleSave();
}

QVariant SettingsManager::value(const QString &key, const QVariant &defaultValue) const
{
    QMutexLocker locker(&m_mutex);

    QStringList parts = key.split('/');
    if (parts.size() < 2) {
        return m_root.contains(key)
            ? m_root.value(key).toVariant()
            : defaultValue;
    }

    QJsonObject group = m_root.value(parts[0]).toObject();
    QString field = parts[1];
    return group.contains(field)
        ? group.value(field).toVariant()
        : defaultValue;
}

bool SettingsManager::contains(const QString &key) const
{
    QMutexLocker locker(&m_mutex);

    QStringList parts = key.split('/');
    if (parts.size() < 2) {
        return m_root.contains(key);
    }

    QJsonObject group = m_root.value(parts[0]).toObject();
    return group.contains(parts[1]);
}

void SettingsManager::remove(const QString &key)
{
    QMutexLocker locker(&m_mutex);

    QStringList parts = key.split('/');
    if (parts.size() < 2) {
        m_root.remove(key);
    } else {
        QJsonObject group = m_root.value(parts[0]).toObject();
        group.remove(parts[1]);
        m_root[parts[0]] = group;
    }

    m_isModified = true;
    scheduleSave();
}

QStringList SettingsManager::childKeys(const QString &group) const
{
    QMutexLocker locker(&m_mutex);

    QJsonObject obj = m_root.value(group).toObject();
    return obj.keys();
}

// ============================================================
// 便捷类型方法
// ============================================================

void SettingsManager::setString(const QString &key, const QString &value)
{
    setValue(key, value);
}

QString SettingsManager::getString(const QString &key, const QString &defaultValue) const
{
    return value(key, defaultValue).toString();
}

void SettingsManager::setInt(const QString &key, int value)
{
    setValue(key, value);
}

int SettingsManager::getInt(const QString &key, int defaultValue) const
{
    return value(key, defaultValue).toInt();
}

void SettingsManager::setBool(const QString &key, bool value)
{
    setValue(key, value);
}

bool SettingsManager::getBool(const QString &key, bool defaultValue) const
{
    return value(key, defaultValue).toBool();
}

// ============================================================
// 连接历史
// ============================================================

QJsonArray SettingsManager::connectionHistory() const
{
    QMutexLocker locker(&m_mutex);
    return m_root.value("ConnectionHistory").toArray();
}

void SettingsManager::setConnectionHistory(const QJsonArray &entries)
{
    QMutexLocker locker(&m_mutex);
    m_root["ConnectionHistory"] = entries;
    m_isModified = true;
    scheduleSave();
}

// ============================================================
// 去抖保存
// ============================================================

void SettingsManager::scheduleSave()
{
    if (m_saveTimer && !m_saveTimer->isActive()) {
        m_saveTimer->start();
    }
}

// ============================================================
// 旧数据迁移（仅执行一次）
// ============================================================

namespace {
constexpr const char *ENC_PREFIX = "ENC:";
constexpr int ENC_PREFIX_LEN = 4;

/** 安全读取旧 QSettings 字符串值 */
static QString oldStr(QSettings &s, const QString &key, const QString &def = {})
{
    return s.value(key, def).toString();
}
} // anonymous namespace

void SettingsManager::migrateFromQSettings()
{
    // 检测旧数据 — 使用旧 App 名称，确保跨版本迁移能找到历史数据
    // （APP_ORGANIZATION/APP_NAME 可能随品牌更名而变化，迁移路径必须硬编码旧值）
    QSettings old(QStringLiteral("CrossRemoteDesktop"),
                  QStringLiteral("Cross Remote Desktop"));
    if (old.allKeys().isEmpty()) {
        qCInfo(lcCoreConfig) << "SettingsManager: No old QSettings data, starting fresh";
        m_migrationDone = true;
        return;
    }

    qCInfo(lcCoreConfig) << "SettingsManager: Migrating from QSettings to JSON...";

    // ── 读取 General ──
    {
        QJsonObject general;
        QString lang = oldStr(old, "General/language", "zh_CN");
        general["language"] = lang;
        general["startWithSystem"] = old.value("General/startWithSystem", false).toBool();
        m_root["General"] = general;
    }

    // ── 读取 UI ──
    {
        QJsonObject ui;
        ui["theme"] = oldStr(old, "UI/theme", "dark");
        ui["closeToTray"] = old.value("UI/closeToTray", false).toBool();
        m_root["UI"] = ui;
    }

    // ── 读取 Server ──
    {
        QJsonObject server;
        server["listenPort"] = old.value("Server/listenPort", 5921).toInt();
        server["username"] = oldStr(old, "Server/username");
        server["password"] = oldStr(old, "Server/password");
        m_root["Server"] = server;
    }

    // ── 读取 Logging ──
    {
        QJsonObject logging;
        logging["level"] = oldStr(old, "Logging/level", "info");
        logging["rules"] = oldStr(old, "Logging/rules");
        m_root["Logging"] = logging;
    }

    // ── 读取 ConnectionHistory ──
    {
        old.beginGroup("ConnectionHistory");
        QStringList hosts       = old.value("hosts").toStringList();
        QStringList hostnames   = old.value("hostnames").toStringList();
        QStringList ports       = old.value("ports").toStringList();
        QStringList times       = old.value("times").toStringList();
        QStringList usernames   = old.value("usernames").toStringList();
        QStringList passwords   = old.value("passwords").toStringList();
        QStringList fullScreens  = old.value("fullScreens").toStringList();
        QStringList windowWidths = old.value("windowWidths").toStringList();
        QStringList windowHeights = old.value("windowHeights").toStringList();
        QStringList colorDepths  = old.value("colorDepths").toStringList();
        QStringList imageQualities = old.value("imageQualities").toStringList();
        QStringList viewOnlys    = old.value("viewOnlys").toStringList();
        QStringList shareClipboards = old.value("shareClipboards").toStringList();
        QStringList showCursors  = old.value("showCursors").toStringList();
        QStringList connectionTimeouts   = old.value("connectionTimeouts").toStringList();
        QStringList autoReconnects       = old.value("autoReconnects").toStringList();
        QStringList reconnectIntervals   = old.value("reconnectIntervals").toStringList();
        old.endGroup();

        QJsonArray history;
        const int count = hosts.size();
        for (int i = 0; i < count; ++i) {
            bool ok = false;
            int port = ports.at(i).toInt(&ok);
            if (!ok || port <= 0 || port > 65535) continue;

            QDateTime time = QDateTime::fromString(times.at(i), Qt::ISODate);
            if (!time.isValid()) continue;

            QJsonObject entry;
            entry["host"]       = hosts.at(i);
            entry["hostname"]   = (i < hostnames.size() && !hostnames.at(i).isEmpty())
                                         ? hostnames.at(i) : hosts.at(i);
            entry["port"]       = port;
            entry["lastConnected"] = time.toString(Qt::ISODate);

            auto safeStr = [](const QStringList &list, int idx, const QString &def = {}) -> QString {
                return (idx < list.size()) ? list.at(idx) : def;
            };
            auto safeBool = [](const QStringList &list, int idx, bool def = false) -> bool {
                return (idx < list.size()) ? QVariant(list.at(idx)).toBool() : def;
            };
            auto safeInt = [](const QStringList &list, int idx, int def = 0) -> int {
                return (idx < list.size()) ? list.at(idx).toInt() : def;
            };

            entry["username"] = safeStr(usernames, i);
            QString storedPass = safeStr(passwords, i);
            if (storedPass.startsWith(QLatin1String(ENC_PREFIX))) {
                entry["password"] = PasswordCrypto::decrypt(
                    safeStr(usernames, i), storedPass.mid(ENC_PREFIX_LEN));
            } else {
                entry["password"] = storedPass;
            }
            entry["fullScreen"]       = safeBool(fullScreens, i);
            entry["windowWidth"]      = safeInt(windowWidths, i, 1600);
            entry["windowHeight"]     = safeInt(windowHeights, i, 900);
            entry["colorDepth"]       = safeInt(colorDepths, i, 32);
            entry["imageQuality"]     = safeInt(imageQualities, i, 85);
            entry["viewOnly"]         = safeBool(viewOnlys, i);
            entry["shareClipboard"]   = safeBool(shareClipboards, i, true);
            entry["showCursor"]       = safeBool(showCursors, i, true);
            entry["connectionTimeout"] = safeInt(connectionTimeouts, i, 30000);
            entry["autoReconnect"]     = safeBool(autoReconnects, i);
            entry["reconnectInterval"] = safeInt(reconnectIntervals, i, 5000);

            history.append(entry);
        }
        m_root["ConnectionHistory"] = history;
    }

    m_root["version"] = QStringLiteral("1.0");
    m_migrationDone = true;

    // ── 清理旧数据 ──
#ifdef Q_OS_WIN
    // Windows: 清空注册表项
    old.clear();
    old.sync();
#else
    // macOS / Linux: 删除旧 QSettings 文件
    QString oldPath = old.fileName();
    old.clear();
    old.sync();
    if (!oldPath.isEmpty()) {
        QFile::remove(oldPath);
    }
#endif

    qCInfo(lcCoreConfig) << "SettingsManager: Migration complete."
                          << "Cleaned old QSettings storage.";
}
