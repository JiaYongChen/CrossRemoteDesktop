#include "SettingsManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMutexLocker>
#include <QtCore/QSaveFile>
#include <QtCore/QSettings>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

#include "common/config/ConnectionHistory.h"
#include "common/logging/LoggingCategories.h"

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
// 加载 / 保存
// ============================================================

bool SettingsManager::load()
{
    QMutexLocker locker(&m_mutex);

    QFileInfo fi(m_filePath);
    if (!fi.exists()) {
        // JSON 不存在 → 尝试从旧 QSettings 迁移
        const bool migrated = migrateFromQSettings();
        m_isModified = true;
        // 旧数据清理延迟到任意一次 saveLocked() 成功后执行（见 saveLocked 尾部），
        // 保证"新 JSON 落盘成功才销毁旧数据"在首次写盘失败、后续去抖/析构保存成功时依然闭环
        m_pendingLegacyCleanup = migrated;
        // 调用者已持锁，用内部版本避免死锁；成功时会顺带清理旧 QSettings（见 saveLocked()）
        const bool persisted = saveLocked();
        if (migrated && !persisted) {
            qCWarning(lcCoreConfig) << "SettingsManager: Failed to persist migrated config,"
                                    << "legacy QSettings data kept until next successful save";
        }
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
        // 产品决策：不做损坏文件备份（.bak），解析失败视为全新启动，
        // 后续保存将覆写损坏文件——saveLocked() 的 QSaveFile 原子写入已从源头消除截断损坏
        qCCritical(lcCoreConfig) << "SettingsManager: JSON parse error:"
                                 << parseError.errorString()
                                 << "- config will be reset on next save";
        return false;
    }

    if (!doc.isObject()) {
        qCCritical(lcCoreConfig) << "SettingsManager: JSON root is not an object"
                                 << "- config will be reset on next save";
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

    // QSaveFile 原子写入：先写临时文件，commit() 时原子替换目标文件，
    // 进程中断不会留下截断的半个 JSON
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcCoreConfig) << "SettingsManager: Cannot open config file for writing:"
                                << m_filePath;
        return false;
    }

    QJsonDocument doc(m_root);
    file.write(doc.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(lcCoreConfig) << "SettingsManager: Failed to commit config file:"
                                << m_filePath << file.errorString();
        return false;
    }

    m_isModified = false;

    // 迁移数据已确认落盘 → 此时销毁旧 QSettings 数据才是安全的
    if (m_pendingLegacyCleanup) {
        m_pendingLegacyCleanup = false;
        clearLegacyQSettings();
    }

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
// 受信任主机（TOFU 服务端证书指纹）
// ============================================================

QJsonArray SettingsManager::trustedHosts() const
{
    QMutexLocker locker(&m_mutex);
    return m_root.value("TrustedHosts").toArray();
}

void SettingsManager::setTrustedHosts(const QJsonArray &entries)
{
    QMutexLocker locker(&m_mutex);
    m_root["TrustedHosts"] = entries;
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
/** 安全读取旧 QSettings 字符串值 */
QString oldStr(QSettings &s, const QString &key, const QString &def = {})
{
    return s.value(key, def).toString();
}
} // anonymous namespace

bool SettingsManager::migrateFromQSettings()
{
    // 检测旧数据 — 使用旧 App 名称，确保跨版本迁移能找到历史数据
    // （APP_ORGANIZATION/APP_NAME 可能随品牌更名而变化，迁移路径必须硬编码旧值）
    QSettings old(QStringLiteral("CrossRemoteDesktop"),
                  QStringLiteral("Cross Remote Desktop"));
    if (old.allKeys().isEmpty()) {
        qCInfo(lcCoreConfig) << "SettingsManager: No old QSettings data, starting fresh";
        return false;
    }

    qCInfo(lcCoreConfig) << "SettingsManager: Migrating from QSettings to JSON...";

    // ── 读取 General ──
    {
        QJsonObject general;
        general["language"] = oldStr(old, "General/language", "zh_CN");
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
    m_root["ConnectionHistory"] = readLegacyConnectionHistory(old);

    m_root["version"] = QStringLiteral("1.0");
    qCInfo(lcCoreConfig) << "SettingsManager: Migration data prepared";
    return true;
}

void SettingsManager::clearLegacyQSettings()
{
    QSettings old(QStringLiteral("CrossRemoteDesktop"),
                  QStringLiteral("Cross Remote Desktop"));
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

QJsonArray SettingsManager::readLegacyConnectionHistory(QSettings &oldSettings)
{
    oldSettings.beginGroup("ConnectionHistory");
    const QStringList hosts       = oldSettings.value("hosts").toStringList();
    const QStringList hostnames   = oldSettings.value("hostnames").toStringList();
    const QStringList ports       = oldSettings.value("ports").toStringList();
    const QStringList times       = oldSettings.value("times").toStringList();
    const QStringList usernames   = oldSettings.value("usernames").toStringList();
    const QStringList passwords   = oldSettings.value("passwords").toStringList();
    const QStringList fullScreens  = oldSettings.value("fullScreens").toStringList();
    const QStringList windowWidths = oldSettings.value("windowWidths").toStringList();
    const QStringList windowHeights = oldSettings.value("windowHeights").toStringList();
    const QStringList colorDepths  = oldSettings.value("colorDepths").toStringList();
    const QStringList imageQualities = oldSettings.value("imageQualities").toStringList();
    const QStringList viewOnlys    = oldSettings.value("viewOnlys").toStringList();
    const QStringList shareClipboards = oldSettings.value("shareClipboards").toStringList();
    const QStringList showCursors  = oldSettings.value("showCursors").toStringList();
    const QStringList connectionTimeouts   = oldSettings.value("connectionTimeouts").toStringList();
    const QStringList autoReconnects       = oldSettings.value("autoReconnects").toStringList();
    const QStringList reconnectIntervals   = oldSettings.value("reconnectIntervals").toStringList();
    oldSettings.endGroup();

    // 旧格式全部为字符串（int 为数字串，bool 为 "1"/"0"），必须先做类型转换
    // 再写入 JSON——ConnectionHistory::load() 的 QJsonValue::toInt()/toBool()
    // 严格类型化，对 String 类型不做解析。解析失败的可选字段省略键，
    // 由 load() 统一填默认值；port/时间非法则整条剔除（与旧迁移行为一致）。
    QJsonArray raw;
    const int count = hosts.size();
    int skipped = 0;
    for (int i = 0; i < count; ++i) {
        bool portOk = false;
        const int port = ports.value(i).toInt(&portOk);
        if (!portOk || port <= 0 || port > 65535) {
            ++skipped;
            continue;
        }

        const QDateTime time = QDateTime::fromString(times.value(i), Qt::ISODate);
        if (!time.isValid()) {
            ++skipped;
            continue;
        }

        QJsonObject entry;
        entry["host"] = hosts.at(i);
        entry["port"] = port;
        entry["lastConnected"] = time.toString(Qt::ISODate);
        entry["username"] = usernames.value(i);
        entry["password"] = passwords.value(i);

        // hostname 为空时省略键，由 load() 回退到 host
        const QString hostname = hostnames.value(i);
        if (!hostname.isEmpty()) {
            entry["hostname"] = hostname;
        }

        auto setInt = [&entry](const char *key, const QStringList &list, int idx) {
            bool ok = false;
            const int v = list.value(idx).toInt(&ok);
            if (ok) entry[QLatin1String(key)] = v;
        };
        auto setBool = [&entry](const char *key, const QStringList &list, int idx) {
            const QString s = list.value(idx);
            if (!s.isEmpty()) entry[QLatin1String(key)] = QVariant(s).toBool();
        };
        setBool("fullScreen",       fullScreens, i);
        setInt("windowWidth",       windowWidths, i);
        setInt("windowHeight",      windowHeights, i);
        setInt("colorDepth",        colorDepths, i);
        setInt("imageQuality",      imageQualities, i);
        setBool("viewOnly",         viewOnlys, i);
        setBool("shareClipboard",   shareClipboards, i);
        setBool("showCursor",       showCursors, i);
        setInt("connectionTimeout", connectionTimeouts, i);
        setBool("autoReconnect",    autoReconnects, i);
        setInt("reconnectInterval", reconnectIntervals, i);

        raw.append(entry);
    }

    if (skipped > 0) {
        qCWarning(lcCoreConfig) << "SettingsManager: Skipped" << skipped
                                << "invalid legacy connection history entries during migration";
    }

    // 统一处理：解密 + 验证 + 默认值 + 规范化（密码解密后以 ENC: 格式重新加密写回，
    // load() 内部会对其剔除的条目自行告警）
    ConnectionHistory history;
    history.load(raw);
    return history.save();
}
