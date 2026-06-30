#pragma once

#include <QList>
#include <QString>
#include <QDateTime>

class QSettings;

/// 单条历史连接记录
struct HistoryEntry {
    QString host;
    int port = 0;
    QString hostname;       // 可选，未设时用 host 显示
    int resWidth = 0;
    int resHeight = 0;
    QDateTime lastConnected;

    /// 显示名称：优先 hostname，回退 host
    [[nodiscard]] QString displayName() const;

    /// "host:port" 格式字符串
    [[nodiscard]] QString addressPort() const;

    /// 搜索键："host:port"（用于过滤匹配）
    [[nodiscard]] QString searchKey() const;
};

/// 连接历史数据管理（纯数据类，无 Qt 对象继承）
/// 不持有 QSettings，由调用方传入
class ConnectionHistory {
public:
    /// 从 QSettings 加载历史（键名与现有格式兼容）
    void load(const QSettings &settings);

    /// 保存历史到 QSettings
    void save(QSettings &settings) const;

    /// 去重：同 host+port 则更新时间戳 + 置顶；否则新增
    void addOrUpdate(const HistoryEntry &entry);

    /// 移除指定条目，返回是否确实移除了
    [[nodiscard]] bool remove(const QString &host, int port);

    /// 按关键词模糊匹配（host:port、hostname），关键词为空返回全部
    [[nodiscard]] QList<HistoryEntry> filter(const QString &keyword) const;

    /// 全部条目（按时间倒序：最新在前）
    [[nodiscard]] const QList<HistoryEntry> &entries() const;

    /// 无任何历史记录
    [[nodiscard]] bool isEmpty() const;

private:
    QList<HistoryEntry> m_entries;
};
