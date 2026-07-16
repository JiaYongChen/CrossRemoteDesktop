#include "ConnectionHistory.h"
#include "common/logging/LoggingCategories.h"
#include "common/crypto/PasswordCrypto.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
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

void ConnectionHistory::load(const QJsonArray &entries)
{
    m_entries.clear();

    for (const QJsonValue &val : entries) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();

        HistoryEntry entry;
        entry.params.host     = obj.value("host").toString();
        entry.params.port     = obj.value("port").toInt(5921);
        entry.params.hostname = obj.value("hostname").toString(entry.params.host);

        if (entry.params.host.isEmpty() || entry.params.port <= 0) continue;

        entry.params.username = obj.value("username").toString();
        QString storedPass = obj.value("password").toString();
        if (storedPass.startsWith(QLatin1String("ENC:"))) {
            entry.params.password = PasswordCrypto::decrypt(
                entry.params.username, storedPass.mid(4));
        } else {
            entry.params.password = storedPass;
        }
        entry.params.fullScreen       = obj.value("fullScreen").toBool(false);
        entry.params.windowWidth      = obj.value("windowWidth").toInt(1600);
        entry.params.windowHeight     = obj.value("windowHeight").toInt(900);
        entry.params.colorDepth       = obj.value("colorDepth").toInt(32);
        entry.params.imageQuality     = obj.value("imageQuality").toInt(85);
        entry.params.viewOnly         = obj.value("viewOnly").toBool(false);
        entry.params.shareClipboard   = obj.value("shareClipboard").toBool(true);
        entry.params.showCursor       = obj.value("showCursor").toBool(true);
        entry.params.connectionTimeout = obj.value("connectionTimeout").toInt(30000);
        entry.params.autoReconnect    = obj.value("autoReconnect").toBool(false);
        entry.params.reconnectInterval = obj.value("reconnectInterval").toInt(5000);

        entry.lastConnected = QDateTime::fromString(
            obj.value("lastConnected").toString(), Qt::ISODate);
        if (!entry.lastConnected.isValid()) {
            entry.lastConnected = QDateTime::currentDateTime();
        }

        m_entries.append(entry);
    }
}

QJsonArray ConnectionHistory::save() const
{
    QJsonArray result;
    for (const auto &e : m_entries) {
        QJsonObject obj;
        obj["host"]     = e.params.host;
        obj["hostname"] = e.params.hostname;
        obj["port"]     = e.params.port;
        obj["username"] = e.params.username;
        if (!e.params.password.isEmpty()) {
            obj["password"] = QLatin1String("ENC:")
                              + PasswordCrypto::encrypt(e.params.username, e.params.password);
        } else {
            obj["password"] = QString();
        }
        obj["fullScreen"]       = e.params.fullScreen;
        obj["windowWidth"]      = e.params.windowWidth;
        obj["windowHeight"]     = e.params.windowHeight;
        obj["colorDepth"]       = e.params.colorDepth;
        obj["imageQuality"]     = e.params.imageQuality;
        obj["viewOnly"]         = e.params.viewOnly;
        obj["shareClipboard"]   = e.params.shareClipboard;
        obj["showCursor"]       = e.params.showCursor;
        obj["connectionTimeout"] = e.params.connectionTimeout;
        obj["autoReconnect"]     = e.params.autoReconnect;
        obj["reconnectInterval"] = e.params.reconnectInterval;
        obj["lastConnected"]     = e.lastConnected.toString(Qt::ISODate);
        result.append(obj);
    }
    return result;
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

