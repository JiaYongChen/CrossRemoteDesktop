#include "ConnectionHistory.h"
#include "../core/logging/LoggingCategories.h"
#include "../core/crypto/PasswordCrypto.h"
#include <QSettings>
#include <QVariant>
#include <algorithm>

// ============================================================
// HistoryEntry 方法实现
// ============================================================

QString HistoryEntry::displayName() const
{
    return params.hostname.isEmpty() ? params.host : params.hostname;
}

QString HistoryEntry::addressPort() const
{
    return QStringLiteral("%1:%2").arg(params.host).arg(params.port);
}

QString HistoryEntry::searchKey() const
{
    return QStringLiteral("%1:%2").arg(params.host).arg(params.port);
}

// ============================================================
// ConnectionHistory 方法实现
// ============================================================

namespace {
constexpr const char* ENC_PREFIX = "ENC:";
constexpr int ENC_PREFIX_LEN = 4;
} // anonymous namespace

void ConnectionHistory::load(QSettings &settings)
{
    m_entries.clear();
    settings.beginGroup("ConnectionHistory");

    const QStringList hosts       = settings.value("hosts").toStringList();
    const QStringList hostnames   = settings.value("hostnames").toStringList();
    const QStringList ports       = settings.value("ports").toStringList();
    const QStringList times       = settings.value("times").toStringList();
    // ── ConnectionParams 全字段 ──
    const QStringList usernames   = settings.value("usernames").toStringList();
    const QStringList passwords   = settings.value("passwords").toStringList();
    const QStringList fullScreens  = settings.value("fullScreens").toStringList();
    const QStringList windowWidths = settings.value("windowWidths").toStringList();
    const QStringList windowHeights = settings.value("windowHeights").toStringList();
    const QStringList colorDepths  = settings.value("colorDepths").toStringList();
    const QStringList imageQualities = settings.value("imageQualities").toStringList();
    const QStringList viewOnlys    = settings.value("viewOnlys").toStringList();
    const QStringList shareClipboards = settings.value("shareClipboards").toStringList();
    const QStringList showCursors  = settings.value("showCursors").toStringList();
    const QStringList connectionTimeouts   = settings.value("connectionTimeouts").toStringList();
    const QStringList autoReconnects       = settings.value("autoReconnects").toStringList();
    const QStringList reconnectIntervals   = settings.value("reconnectIntervals").toStringList();

    const int count = hosts.size();
    for (int i = 0; i < count; ++i) {
        bool ok = false;
        int port = ports.at(i).toInt(&ok);
        if (!ok || port <= 0 || port > 65535) continue;

        QDateTime time = QDateTime::fromString(times.at(i), Qt::ISODate);
        if (!time.isValid()) continue;

        HistoryEntry entry;
        entry.params.host     = hosts.at(i);
        entry.params.port     = port;
        entry.params.hostname = (i < hostnames.size() && !hostnames.at(i).isEmpty())
            ? hostnames.at(i) : hosts.at(i);
        const QString storedUser = (i < usernames.size()) ? usernames.at(i) : QString();
        const QString storedPass = (i < passwords.size()) ? passwords.at(i) : QString();
        entry.params.username = storedUser;
        // ENC: 前缀 → 密文解密；无前缀 → 兼容旧版明文密码
        if (storedPass.startsWith(QLatin1String(ENC_PREFIX))) {
            entry.params.password = PasswordCrypto::decrypt(storedUser, storedPass.mid(ENC_PREFIX_LEN));
        } else {
            entry.params.password = storedPass;
        }
        entry.params.fullScreen       = (i < fullScreens.size())       ? QVariant(fullScreens.at(i)).toBool()   : false;
        entry.params.windowWidth      = (i < windowWidths.size())      ? windowWidths.at(i).toInt()              : 1600;
        entry.params.windowHeight     = (i < windowHeights.size())     ? windowHeights.at(i).toInt()             : 900;
        entry.params.colorDepth       = (i < colorDepths.size())       ? colorDepths.at(i).toInt()               : 32;
        entry.params.imageQuality     = (i < imageQualities.size())    ? imageQualities.at(i).toInt()            : 85;
        entry.params.viewOnly         = (i < viewOnlys.size())         ? QVariant(viewOnlys.at(i)).toBool()     : false;
        entry.params.shareClipboard   = (i < shareClipboards.size())   ? QVariant(shareClipboards.at(i)).toBool() : true;
        entry.params.showCursor       = (i < showCursors.size())       ? QVariant(showCursors.at(i)).toBool()   : true;
        entry.params.connectionTimeout = (i < connectionTimeouts.size()) ? connectionTimeouts.at(i).toInt()     : 30000;
        entry.params.autoReconnect    = (i < autoReconnects.size())     ? QVariant(autoReconnects.at(i)).toBool() : false;
        entry.params.reconnectInterval = (i < reconnectIntervals.size()) ? reconnectIntervals.at(i).toInt()     : 5000;
        entry.lastConnected = time;
        m_entries.append(entry);
    }
    settings.endGroup();

    // 数组长度不一致时的警告（字段扩展后可能更多不一致）
    const int maxSize = std::max({hosts.size(), hostnames.size(), ports.size(), times.size()});
    if (hosts.size() != maxSize || ports.size() != maxSize || times.size() != maxSize) {
        qCWarning(lcUIMainWindow) << "ConnectionHistory::load - QSettings arrays inconsistent"
                                  << "-> truncating to" << count;
    }
}

void ConnectionHistory::save(QSettings &settings) const
{
    QStringList hosts, hostnames, ports, times;
    QStringList usernames, passwords;
    QStringList fullScreens, windowWidths, windowHeights, colorDepths;
    QStringList imageQualities, viewOnlys, shareClipboards, showCursors;
    QStringList connectionTimeouts, autoReconnects, reconnectIntervals;

    for (const auto &e : m_entries) {
        hosts.append(e.params.host);
        hostnames.append(e.params.hostname);
        ports.append(QString::number(e.params.port));
        times.append(e.lastConnected.toString(Qt::ISODate));
        usernames.append(e.params.username);
        // 密码加密存储：ENC: 前缀 + AES-256-CBC 密文
        if (!e.params.password.isEmpty()) {
            passwords.append(QLatin1String(ENC_PREFIX)
                             + PasswordCrypto::encrypt(e.params.username, e.params.password));
        } else {
            passwords.append(QString());
        }
        fullScreens.append(e.params.fullScreen ? "1" : "0");
        windowWidths.append(QString::number(e.params.windowWidth));
        windowHeights.append(QString::number(e.params.windowHeight));
        colorDepths.append(QString::number(e.params.colorDepth));
        imageQualities.append(QString::number(e.params.imageQuality));
        viewOnlys.append(e.params.viewOnly ? "1" : "0");
        shareClipboards.append(e.params.shareClipboard ? "1" : "0");
        showCursors.append(e.params.showCursor ? "1" : "0");
        connectionTimeouts.append(QString::number(e.params.connectionTimeout));
        autoReconnects.append(e.params.autoReconnect ? "1" : "0");
        reconnectIntervals.append(QString::number(e.params.reconnectInterval));
    }

    settings.beginGroup("ConnectionHistory");
    settings.setValue("hosts", hosts);
    settings.setValue("hostnames", hostnames);
    settings.setValue("ports", ports);
    settings.setValue("times", times);
    settings.setValue("usernames", usernames);
    settings.setValue("passwords", passwords);
    settings.setValue("fullScreens", fullScreens);
    settings.setValue("windowWidths", windowWidths);
    settings.setValue("windowHeights", windowHeights);
    settings.setValue("colorDepths", colorDepths);
    settings.setValue("imageQualities", imageQualities);
    settings.setValue("viewOnlys", viewOnlys);
    settings.setValue("shareClipboards", shareClipboards);
    settings.setValue("showCursors", showCursors);
    settings.setValue("connectionTimeouts", connectionTimeouts);
    settings.setValue("autoReconnects", autoReconnects);
    settings.setValue("reconnectIntervals", reconnectIntervals);
    settings.endGroup();
    settings.sync();
}

void ConnectionHistory::addOrUpdate(const HistoryEntry &entry)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].params.host == entry.params.host
            && m_entries[i].params.port == entry.params.port) {
            m_entries[i].params = entry.params;        // 全部配置覆盖
            m_entries[i].lastConnected = entry.lastConnected;
            m_entries.move(i, 0);
            return;
        }
    }
    m_entries.prepend(entry);
}

bool ConnectionHistory::remove(const QString &host, int port)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].params.host == host && m_entries[i].params.port == port) {
            m_entries.removeAt(i);
            return true;
        }
    }
    return false;
}

QList<HistoryEntry> ConnectionHistory::filter(const QString &keyword) const
{
    if (keyword.isEmpty())
        return m_entries;

    QList<HistoryEntry> result;
    for (const auto &e : m_entries) {
        if (e.searchKey().contains(keyword, Qt::CaseInsensitive)
            || e.params.hostname.contains(keyword, Qt::CaseInsensitive)) {
            result.append(e);
        }
    }
    return result;
}

const QList<HistoryEntry> &ConnectionHistory::entries() const
{
    return m_entries;
}

bool ConnectionHistory::isEmpty() const
{
    return m_entries.isEmpty();
}
