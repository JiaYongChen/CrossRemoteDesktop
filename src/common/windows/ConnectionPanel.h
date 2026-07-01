#pragma once

#include <QWidget>
#include "ConnectionHistory.h"

class QLineEdit;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class ConnectionCard;
class QSettings;

/// 连接历史面板：搜索框 + 卡片列表 + 空状态
/// 持有 ConnectionHistory 数据层，通过信号与父级通信
class ConnectionPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ConnectionPanel(QWidget *parent = nullptr);

    /// 从 QSettings 加载历史并填充卡片
    void loadHistory(const QSettings &settings);

    /// 将当前历史保存到 QSettings
    void saveHistory(QSettings &settings) const;

    /// 添加或更新一条连接记录（同 host+port 则更新置顶）
    void addEntry(const QString &host, int port,
                  const QString &hostname = {},
                  int resWidth = 0, int resHeight = 0);

    /// 移除指定条目（编辑→确认后删除旧条目时使用）
    void removeEntry(const QString &host, int port);

    /// 获取指定条目的数据（MainWindow 编辑预填时使用）
    [[nodiscard]] HistoryEntry entryFor(const QString &host, int port) const;

    void retranslateUi();

signals:
    /// 用户点击连接按钮 → MainWindow 构造 ConnectionParams 并发起连接
    void connectRequested(const QString &host, int port, const QString &hostname);

    /// 用户点击编辑按钮 → MainWindow 打开 ConnectionDialog 预填参数
    void editRequested(const QString &host, int port);

    /// 卡片列表内容发生变更（增/删）→ MainWindow 调用 saveHistory
    void contentChanged();

private slots:
    void onSearchTextChanged(const QString &text);

private:
    void setupUi();

    /// 在 m_cards 中查找匹配的卡片
    [[nodiscard]] ConnectionCard *findCard(const QString &host, int port) const;

    /// 从 HistoryEntry 构造新的 ConnectionCard 并完成信号连接
    [[nodiscard]] ConnectionCard *createCard(const HistoryEntry &entry);

    /// 根据搜索状态更新空状态标签文案
    void updateEmptyState();

    QLineEdit *m_searchBox = nullptr;
    QLabel *m_emptyStateLabel = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_cardContainer = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;

    ConnectionHistory m_history;
    QList<ConnectionCard *> m_cards;
};
