#include "ConnectionHistory.h"
#include "../core/logging/LoggingCategories.h"
#include <QSettings>
#include <algorithm>

// ============================================================
// HistoryEntry 方法实现
// ============================================================

QString HistoryEntry::displayName() const
{
    return hostname.isEmpty() ? host : hostname;
}

QString HistoryEntry::addressPort() const
{
    return QStringLiteral("%1:%2").arg(host).arg(port);
}

QString HistoryEntry::searchKey() const
{
    return QStringLiteral("%1:%2").arg(host).arg(port);
}

// ============================================================
// ConnectionHistory 方法实现
// ============================================================

void ConnectionHistory::load(const QSettings &settings)
{
    m_entries.clear();

    // 使用全限定键路径，兼容现有 QSettings 格式
    const QStringList hosts = settings.value("ConnectionHistory/hosts").toStringList();
    const QStringList hostnames = settings.value("ConnectionHistory/hostnames").toStringList();
    const QStringList ports = settings.value("ConnectionHistory/ports").toStringList();
    const QStringList times = settings.value("ConnectionHistory/times").toStringList();
    const QStringList resWidths = settings.value("ConnectionHistory/resWidths").toStringList();
    const QStringList resHeights = settings.value("ConnectionHistory/resHeights").toStringList();

    const int count = qMin(hosts.size(), qMin(ports.size(), times.size()));

    // 数组长度不一致时输出警告，帮助排查 QSettings 数据损坏
    const int maxSize = std::max({hosts.size(), hostnames.size(), ports.size(),
                                  times.size(), resWidths.size(), resHeights.size()});
    if (hosts.size() != maxSize || ports.size() != maxSize || times.size() != maxSize) {
        qCWarning(lcUIMainWindow) << "ConnectionHistory::load - QSettings arrays inconsistent:"
                                  << "hosts=" << hosts.size()
                                  << "ports=" << ports.size()
                                  << "times=" << times.size()
                                  << "-> truncating to" << count;
    }

    for (int i = 0; i < count; ++i) {
        bool ok = false;
        int port = ports.at(i).toInt(&ok);
        if (!ok || port <= 0 || port > 65535)
            continue;

        QDateTime time = QDateTime::fromString(times.at(i), Qt::ISODate);
        if (!time.isValid())
            continue;

        HistoryEntry entry;
        entry.host = hosts.at(i);
        entry.port = port;
        entry.hostname = (i < hostnames.size() && !hostnames.at(i).isEmpty())
            ? hostnames.at(i) : hosts.at(i);
        entry.resWidth = (i < resWidths.size()) ? resWidths.at(i).toInt() : 0;
        entry.resHeight = (i < resHeights.size()) ? resHeights.at(i).toInt() : 0;
        entry.lastConnected = time;
        m_entries.append(entry);
    }
}

void ConnectionHistory::save(QSettings &settings) const
{
    QStringList hosts, hostnames, ports, times, resWidths, resHeights;
    for (const auto &e : m_entries) {
        hosts.append(e.host);
        hostnames.append(e.hostname);
        ports.append(QString::number(e.port));
        times.append(e.lastConnected.toString(Qt::ISODate));
        resWidths.append(QString::number(e.resWidth));
        resHeights.append(QString::number(e.resHeight));
    }
    settings.beginGroup("ConnectionHistory");
    settings.setValue("hosts", hosts);
    settings.setValue("hostnames", hostnames);
    settings.setValue("ports", ports);
    settings.setValue("times", times);
    settings.setValue("resWidths", resWidths);
    settings.setValue("resHeights", resHeights);
    settings.endGroup();
    settings.sync();
}

void ConnectionHistory::addOrUpdate(const HistoryEntry &entry)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].host == entry.host && m_entries[i].port == entry.port) {
            // 更新时间戳和分辨率并移到顶部
            m_entries[i].lastConnected = entry.lastConnected;
            m_entries[i].resWidth = entry.resWidth;
            m_entries[i].resHeight = entry.resHeight;
            if (!entry.hostname.isEmpty()) {
                m_entries[i].hostname = entry.hostname;
            }
            m_entries.move(i, 0);
            return;
        }
    }
    // 新条目：插入顶部
    m_entries.prepend(entry);
}

bool ConnectionHistory::remove(const QString &host, int port)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].host == host && m_entries[i].port == port) {
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
            || e.hostname.contains(keyword, Qt::CaseInsensitive)) {
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
