#include "ConnectionPanel.h"
#include "ui_ConnectionPanel.h"
#include "ConnectionCard.h"
#include "../core/logging/LoggingCategories.h"

#include <QLineEdit>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include "common/core/config/SettingsManager.h"
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

    // 使 QWidget 通过样式表绘制背景（需配合 QSS #connectionPanel 规则）
    setAttribute(Qt::WA_StyledBackground, true);

    // QScrollArea 内部 viewport 为普通 QWidget，需同时开启 WA_StyledBackground
    // + setStyleSheet 才能让 QSS 透明背景生效。仅 setAutoFillBackground(false)
    // 在全局 QSS 激活时不够——QSS 引擎仍会从 palette 回退绘制不透明背景
    // 使用 palette 设置视口透明背景，避免 setStyleSheet 阻断子控件的 QSS 级联
    QWidget *vp = ui->scrollArea->viewport();
    vp->setAutoFillBackground(true);
    vp->setAttribute(Qt::WA_StyledBackground, true);
    QPalette pal = vp->palette();
    pal.setColor(QPalette::Window, Qt::transparent);
    vp->setPalette(pal);

    // 搜索实时过滤
    connect(ui->searchBox, &QLineEdit::textChanged,
            this, &ConnectionPanel::onSearchTextChanged);
}

ConnectionPanel::~ConnectionPanel()
{
    delete ui;
}

// ============================================================
// 公开接口
// ============================================================

void ConnectionPanel::loadHistory(SettingsManager &sm)
{
    qDeleteAll(m_cards);
    m_cards.clear();

    m_history.load(sm.connectionHistory());
    const auto &entries = m_history.entries();
    for (int i = entries.size() - 1; i >= 0; --i) {
        (void)createCard(entries[i]);
    }
    updateEmptyState();
    qCInfo(lcUIMainWindow) << "ConnectionPanel::loadHistory - Loaded"
                           << m_cards.size() << "connection(s)";
}

void ConnectionPanel::saveHistory(SettingsManager &sm) const
{
    sm.setConnectionHistory(m_history.save());
}

void ConnectionPanel::addEntry(const ConnectionParams& params)
{
    const QDateTime now = QDateTime::currentDateTime();

    HistoryEntry entry;
    entry.params = params;
    entry.lastConnected = now;

    ConnectionCard *existing = findCard(params.host, params.port);
    if (existing) {
        existing->setHostname(entry.displayName());
        existing->setProperty("hostname", entry.displayName());
        existing->setProperty("searchKey", entry.searchKey());
        existing->setAddressPort(params.host, params.port);
        existing->setResolution(params.windowWidth, params.windowHeight);
        existing->setLastConnected(now);
        existing->setProperty("resWidth", params.windowWidth);
        existing->setProperty("resHeight", params.windowHeight);
        auto *cl = cardLayout();
        cl->removeWidget(existing);
        cl->insertWidget(0, existing);
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
        cardLayout()->removeWidget(card);
        m_cards.removeOne(card);
        card->deleteLater();
    }
    (void)m_history.remove(host, port);
    updateEmptyState();
}

HistoryEntry ConnectionPanel::entryFor(const QString &host, int port) const
{
    for (const auto &e : m_history.entries()) {
        if (e.params.host == host && e.params.port == port)
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

QVBoxLayout *ConnectionPanel::cardLayout() const
{
    return qobject_cast<QVBoxLayout *>(ui->cardContainer->layout());
}

[[nodiscard]] ConnectionCard *ConnectionPanel::createCard(const HistoryEntry &entry)
{
    auto *card = new ConnectionCard();
    card->setHostname(entry.displayName());
    card->setAddressPort(entry.params.host, entry.params.port);
    card->setResolution(entry.params.windowWidth, entry.params.windowHeight);
    card->setLastConnected(entry.lastConnected);
    card->setProperty("host", entry.params.host);
    card->setProperty("hostname", entry.displayName());
    card->setProperty("port", entry.params.port);
    card->setProperty("time", entry.lastConnected);
    card->setProperty("resWidth", entry.params.windowWidth);
    card->setProperty("resHeight", entry.params.windowHeight);
    card->setProperty("searchKey", entry.searchKey());

    // 连接按钮 → 发射信号（hostname 可能在 addEntry 更新时变化，保持属性读取）
    const QString capturedHost = entry.params.host;
    const int capturedPort = entry.params.port;
    connect(card, &ConnectionCard::connectClicked, this, [this, card, capturedHost, capturedPort]() {
        QString hostname = card->property("hostname").toString();
        emit connectRequested(capturedHost, capturedPort, hostname);
    });

    // 编辑按钮 → 发射信号（host/port 创建后不变，按值捕获）
    connect(card, &ConnectionCard::editClicked, this, [this, capturedHost, capturedPort]() {
        emit editRequested(capturedHost, capturedPort);
    });

    // 删除按钮 → 内部处理（host/port 创建后不变，按值捕获）
    connect(card, &ConnectionCard::deleteClicked, this, [this, card, capturedHost, capturedPort]() {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("确认删除"));
        msgBox.setText(tr("确定删除此连接记录？"));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);

        if (msgBox.exec() == QMessageBox::Yes) {
            removeEntry(capturedHost, capturedPort);
            emit contentChanged();
        }
    });

    // 插入布局顶部 + 列表头部
    cardLayout()->insertWidget(0, card);
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
        matchedKeys.insert({e.params.host, e.params.port});
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
