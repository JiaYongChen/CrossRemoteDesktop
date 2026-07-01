#include "ConnectionPanel.h"
#include "ConnectionCard.h"
#include "../core/logging/LoggingCategories.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QScrollArea>
#include <QSettings>
#include <QDateTime>
#include <QMessageBox>

// ============================================================
// 构造 + UI 构建
// ============================================================

ConnectionPanel::ConnectionPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void ConnectionPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setAlignment(Qt::AlignHCenter);

    // --- 搜索框 ---
    layout->addSpacing(24);
    m_searchBox = new QLineEdit();
    m_searchBox->setObjectName("searchBox");
    m_searchBox->setPlaceholderText(tr("搜索历史连接..."));
    m_searchBox->setFixedWidth(400);
    m_searchBox->setClearButtonEnabled(true);
    layout->addWidget(m_searchBox, 0, Qt::AlignHCenter);

    // --- 空状态提示 ---
    layout->addSpacing(4);
    m_emptyStateLabel = new QLabel(tr("暂无连接历史"));
    m_emptyStateLabel->setObjectName("emptyStateLabel");
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_emptyStateLabel, 0, Qt::AlignHCenter);

    // --- 卡片滚动区域 ---
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_cardContainer = new QWidget();
    m_cardLayout = new QVBoxLayout(m_cardContainer);
    m_cardLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_cardLayout->setSpacing(12);
    m_cardLayout->setContentsMargins(0, 0, 0, 0);

    m_scrollArea->setWidget(m_cardContainer);
    layout->addWidget(m_scrollArea, 1);

    // 搜索实时过滤
    connect(m_searchBox, &QLineEdit::textChanged,
            this, &ConnectionPanel::onSearchTextChanged);
}

// ============================================================
// 公开接口
// ============================================================

void ConnectionPanel::loadHistory(const QSettings &settings)
{
    m_history.load(settings);
    for (const auto &entry : m_history.entries()) {
        createCard(entry);
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
    // 先尝试更新已有卡片
    ConnectionCard *existing = findCard(host, port);
    if (existing) {
        existing->setHostname(hostname.isEmpty() ? host : hostname);
        existing->setAddressPort(host, port);
        existing->setResolution(resWidth, resHeight);
        existing->setLastConnected(QDateTime::currentDateTime());
        existing->setProperty("resWidth", resWidth);
        existing->setProperty("resHeight", resHeight);
        // 置顶
        m_cardLayout->removeWidget(existing);
        m_cardLayout->insertWidget(0, existing);
        m_cards.removeOne(existing);
        m_cards.prepend(existing);
    } else {
        HistoryEntry entry;
        entry.host = host;
        entry.port = port;
        entry.hostname = hostname.isEmpty() ? host : hostname;
        entry.resWidth = resWidth;
        entry.resHeight = resHeight;
        entry.lastConnected = QDateTime::currentDateTime();
        createCard(entry);
    }

    // 同步数据层
    HistoryEntry entry;
    entry.host = host;
    entry.port = port;
    entry.hostname = hostname.isEmpty() ? host : hostname;
    entry.resWidth = resWidth;
    entry.resHeight = resHeight;
    entry.lastConnected = QDateTime::currentDateTime();
    m_history.addOrUpdate(entry);

    m_emptyStateLabel->setVisible(false);
}

void ConnectionPanel::removeEntry(const QString &host, int port)
{
    ConnectionCard *card = findCard(host, port);
    if (card) {
        m_cardLayout->removeWidget(card);
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
    m_searchBox->setPlaceholderText(tr("搜索历史连接..."));
    updateEmptyState();
    for (auto *card : m_cards) {
        card->retranslateUi();
    }
}

// ============================================================
// 私有方法
// ============================================================

ConnectionCard *ConnectionPanel::findCard(const QString &host, int port) const
{
    for (auto *card : m_cards) {
        if (card->property("host").toString() == host
            && card->property("port").toInt() == port) {
            return card;
        }
    }
    return nullptr;
}

ConnectionCard *ConnectionPanel::createCard(const HistoryEntry &entry)
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
        }
    });

    // 插入布局顶部 + 列表头部
    m_cardLayout->insertWidget(0, card);
    m_cards.prepend(card);
    return card;
}

void ConnectionPanel::updateEmptyState()
{
    bool empty = m_cards.isEmpty();
    m_emptyStateLabel->setVisible(empty);

    if (empty) {
        m_emptyStateLabel->setText(
            m_searchBox && !m_searchBox->text().isEmpty()
                ? tr("无匹配的连接记录")
                : tr("暂无连接历史"));
    }
}

void ConnectionPanel::onSearchTextChanged(const QString &text)
{
    int visibleCount = 0;
    for (auto *card : m_cards) {
        bool match = text.isEmpty()
            || card->property("searchKey").toString().contains(text, Qt::CaseInsensitive)
            || card->property("hostname").toString().contains(text, Qt::CaseInsensitive);
        card->setVisible(match);
        if (match) visibleCount++;
    }
    m_emptyStateLabel->setVisible(visibleCount == 0);
    m_emptyStateLabel->setText(
        text.isEmpty()
            ? tr("暂无连接历史")
            : tr("无匹配的连接记录"));
}
