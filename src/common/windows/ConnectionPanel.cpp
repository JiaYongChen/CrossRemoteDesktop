#include "ConnectionPanel.h"
#include "ui_ConnectionPanel.h"
#include "ConnectionCard.h"
#include "../core/logging/LoggingCategories.h"

#include <QLineEdit>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QSettings>
#include <QDateTime>
#include <QMessageBox>
#include <QSet>
#include <QPair>

// ============================================================
// 构造 + UI 构建
// ============================================================

ConnectionPanel::ConnectionPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ConnectionPanel)
{
    ui->setupUi(this);

    // 搜索实时过滤
    connect(ui->searchBox, &QLineEdit::textChanged,
            this, &ConnectionPanel::onSearchTextChanged);
}

// ============================================================
// 公开接口
// ============================================================

void ConnectionPanel::loadHistory(const QSettings &settings)
{
    qDeleteAll(m_cards);
    m_cards.clear();

    m_history.load(settings);
    // entries 是 newest-first，createCard 用 prepend 插入顶部。
    // 反向遍历确保最旧的先被 prepend（沉到下面），最新的最后被 prepend（最终在顶部）。
    const auto &entries = m_history.entries();
    for (int i = entries.size() - 1; i >= 0; --i) {
        (void)createCard(entries[i]);
    }
    updateEmptyState();
    qCInfo(lcUIMainWindow) << "ConnectionPanel::loadHistory - Loaded"
                           << m_cards.size() << "connection(s)";
}

void ConnectionPanel::saveHistory(QSettings &settings) const
{
    m_history.save(settings);
}

void ConnectionPanel::addEntry(const QString &host, int port,
                                const QString &hostname,
                                int resWidth, int resHeight)
{
    const QDateTime now = QDateTime::currentDateTime();

    HistoryEntry entry;
    entry.host = host;
    entry.port = port;
    entry.hostname = hostname.isEmpty() ? host : hostname;
    entry.resWidth = resWidth;
    entry.resHeight = resHeight;
    entry.lastConnected = now;

    // 获取布局引用
    auto *cardLayout = qobject_cast<QVBoxLayout *>(ui->cardContainer->layout());

    // 先尝试更新已有卡片
    ConnectionCard *existing = findCard(host, port);
    if (existing) {
        existing->setHostname(entry.hostname);
        existing->setProperty("hostname", entry.hostname);
        existing->setProperty("searchKey", entry.searchKey());
        existing->setAddressPort(host, port);
        existing->setResolution(resWidth, resHeight);
        existing->setLastConnected(now);
        existing->setProperty("resWidth", resWidth);
        existing->setProperty("resHeight", resHeight);
        // 置顶
        cardLayout->removeWidget(existing);
        cardLayout->insertWidget(0, existing);
        m_cards.removeOne(existing);
        m_cards.prepend(existing);
    } else {
        (void)createCard(entry);
    }

    m_history.addOrUpdate(entry);
    updateEmptyState();
}

void ConnectionPanel::removeEntry(const QString &host, int port)
{
    ConnectionCard *card = findCard(host, port);
    if (card) {
        auto *cardLayout = qobject_cast<QVBoxLayout *>(ui->cardContainer->layout());
        cardLayout->removeWidget(card);
        m_cards.removeOne(card);
        card->deleteLater();
    }
    (void)m_history.remove(host, port);
    updateEmptyState();
}

HistoryEntry ConnectionPanel::entryFor(const QString &host, int port) const
{
    for (const auto &e : m_history.entries()) {
        if (e.host == host && e.port == port)
            return e;
    }
    return {};
}

void ConnectionPanel::retranslateUi()
{
    ui->searchBox->setPlaceholderText(tr("搜索历史连接..."));
    updateEmptyState();
    for (auto *card : m_cards) {
        card->retranslateUi();
    }
}

void ConnectionPanel::refreshIcons()
{
    for (auto *card : m_cards) {
        card->refreshIcons();
    }
}

// ============================================================
// 私有方法
// ============================================================

[[nodiscard]] ConnectionCard *ConnectionPanel::findCard(const QString &host, int port) const
{
    for (auto *card : m_cards) {
        if (card->property("host").toString() == host
            && card->property("port").toInt() == port) {
            return card;
        }
    }
    return nullptr;
}

[[nodiscard]] ConnectionCard *ConnectionPanel::createCard(const HistoryEntry &entry)
{
    auto *card = new ConnectionCard();
    card->setHostname(entry.displayName());
    card->setAddressPort(entry.host, entry.port);
    card->setResolution(entry.resWidth, entry.resHeight);
    card->setLastConnected(entry.lastConnected);
    card->setProperty("host", entry.host);
    card->setProperty("hostname", entry.displayName());
    card->setProperty("port", entry.port);
    card->setProperty("time", entry.lastConnected);
    card->setProperty("resWidth", entry.resWidth);
    card->setProperty("resHeight", entry.resHeight);
    card->setProperty("searchKey", entry.searchKey());

    // 连接按钮 → 发射信号
    connect(card, &ConnectionCard::connectClicked, this, [this, card]() {
        QString host = card->property("host").toString();
        int port = card->property("port").toInt();
        QString hostname = card->property("hostname").toString();
        emit connectRequested(host, port, hostname);
    });

    // 编辑按钮 → 发射信号
    connect(card, &ConnectionCard::editClicked, this, [this, card]() {
        QString host = card->property("host").toString();
        int port = card->property("port").toInt();
        emit editRequested(host, port);
    });

    // 删除按钮 → 内部处理
    connect(card, &ConnectionCard::deleteClicked, this, [this, card]() {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("确认删除"));
        msgBox.setText(tr("确定删除此连接记录？"));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);

        if (msgBox.exec() == QMessageBox::Yes) {
            QString host = card->property("host").toString();
            int port = card->property("port").toInt();
            removeEntry(host, port);
            emit contentChanged();
        }
    });

    // 插入布局顶部 + 列表头部
    auto *cardLayout = qobject_cast<QVBoxLayout *>(ui->cardContainer->layout());
    cardLayout->insertWidget(0, card);
    m_cards.prepend(card);
    return card;
}

void ConnectionPanel::updateEmptyState()
{
    bool empty = m_cards.isEmpty();
    ui->emptyStateLabel->setVisible(empty);

    if (empty) {
        ui->emptyStateLabel->setText(
            !ui->searchBox->text().isEmpty()
                ? tr("无匹配的连接记录")
                : tr("暂无连接历史"));
    }
}

void ConnectionPanel::onSearchTextChanged(const QString &text)
{
    const QList<HistoryEntry> matched = m_history.filter(text);

    QSet<QPair<QString, int>> matchedKeys;
    for (const auto &e : matched) {
        matchedKeys.insert({e.host, e.port});
    }

    for (auto *card : m_cards) {
        QPair<QString, int> key = {card->property("host").toString(),
                                    card->property("port").toInt()};
        card->setVisible(matchedKeys.contains(key));
    }

    ui->emptyStateLabel->setVisible(matchedKeys.isEmpty());
    ui->emptyStateLabel->setText(
        text.isEmpty()
            ? tr("暂无连接历史")
            : tr("无匹配的连接记录"));
}
