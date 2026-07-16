#pragma once

#include <QWidget>
#include <QList>
#include "common/config/ConnectionHistory.h"

class ConnectionCard;
class SettingsManager;
class QVBoxLayout;

QT_BEGIN_NAMESPACE
namespace Ui { class ConnectionPanel; }
QT_END_NAMESPACE

/// 连接历史面板：搜索框 + 卡片列表 + 空状态
/// 持有 ConnectionHistory 数据层，通过信号与父级通信
class ConnectionPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ConnectionPanel(QWidget *parent = nullptr);
    ~ConnectionPanel() override;

    /// 从 SettingsManager 加载历史并填充卡片
    void loadHistory(SettingsManager &sm);

    /// 将当前历史保存到 SettingsManager
    void saveHistory(SettingsManager &sm) const;

    /// 添加或更新一个连接条目
    void addEntry(const ConnectionParams& params);

    /// 移除指定条目（编辑→确认后删除旧条目时使用）
    void removeEntry(const QString &host, int port);

    /// 获取指定条目的数据（MainWindow 编辑预填时使用）
    [[nodiscard]] HistoryEntry entryFor(const QString &host, int port) const;

    void retranslateUi();

signals:
    void connectRequested(const QString &host, int port, const QString &hostname);
    void editRequested(const QString &host, int port);
    void contentChanged();

private slots:
    void onSearchTextChanged(const QString &text);

private:
    /// 在 m_cards 中查找匹配的卡片
    [[nodiscard]] ConnectionCard *findCard(const QString &host, int port) const;

    /// 从 HistoryEntry 构造新的 ConnectionCard 并完成信号连接
    [[nodiscard]] ConnectionCard *createCard(const HistoryEntry &entry);

    /// 根据搜索状态更新空状态标签文案
    void updateEmptyState();

    /// 卡片布局缓存（避免重复 qobject_cast）
    [[nodiscard]] QVBoxLayout *cardLayout() const;

    Ui::ConnectionPanel *ui = nullptr;

    ConnectionHistory m_history;
    QList<ConnectionCard *> m_cards;
};
