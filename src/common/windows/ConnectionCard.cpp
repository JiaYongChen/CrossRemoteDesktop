#include "ConnectionCard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

ConnectionCard::ConnectionCard(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("connectionCard");
    setFixedSize(400, 140);
    setupUi();
}

void ConnectionCard::setupUi()
{
    // --- 主垂直布局（左侧信息 + 右侧图标） ---
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(16, 12, 12, 12);
    mainLayout->setSpacing(0);

    // --- 左侧信息区 ---
    auto *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(4);

    // 主机名 + 状态灯
    auto *nameRow = new QHBoxLayout();
    nameRow->setSpacing(6);
    m_statusIndicator = new QLabel();
    m_statusIndicator->setFixedSize(8, 8);
    m_statusIndicator->setStyleSheet(
        "background-color: #9E9E9E; border-radius: 4px;");
    m_hostnameLabel = new QLabel();
    m_hostnameLabel->setObjectName("cardHostname");
    nameRow->addWidget(m_statusIndicator);
    nameRow->addWidget(m_hostnameLabel);
    nameRow->addStretch();
    infoLayout->addLayout(nameRow);

    // 地址:端口
    m_addressLabel = new QLabel();
    m_addressLabel->setObjectName("cardAddress");
    infoLayout->addWidget(m_addressLabel);

    // 分辨率 + 时间
    auto *metaRow = new QHBoxLayout();
    metaRow->setSpacing(16);
    m_resolutionLabel = new QLabel();
    m_resolutionLabel->setObjectName("cardInfo");
    m_timeLabel = new QLabel();
    m_timeLabel->setObjectName("cardInfo");
    metaRow->addWidget(m_resolutionLabel);
    metaRow->addWidget(m_timeLabel);
    metaRow->addStretch();
    infoLayout->addLayout(metaRow);

    mainLayout->addLayout(infoLayout);

    // --- 右侧操作按钮 ---
    auto *actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(4);
    actionLayout->setAlignment(Qt::AlignVCenter);

    m_connectButton = new QToolButton();
    m_connectButton->setIcon(QIcon(":/icons/connect.svg"));
    m_connectButton->setIconSize(QSize(18, 18));
    m_connectButton->setToolTip(tr("连接"));
    m_connectButton->setAutoRaise(true);

    m_editButton = new QToolButton();
    m_editButton->setIcon(QIcon(":/icons/save.svg"));
    m_editButton->setIconSize(QSize(18, 18));
    m_editButton->setToolTip(tr("修改参数"));
    m_editButton->setAutoRaise(true);

    m_deleteButton = new QToolButton();
    m_deleteButton->setIcon(QIcon(":/icons/delete.svg"));
    m_deleteButton->setIconSize(QSize(18, 18));
    m_deleteButton->setToolTip(tr("删除记录"));
    m_deleteButton->setAutoRaise(true);

    actionLayout->addWidget(m_connectButton);
    actionLayout->addWidget(m_editButton);
    actionLayout->addWidget(m_deleteButton);

    mainLayout->addLayout(actionLayout);

    // --- 信号连接 ---
    connect(m_connectButton, &QToolButton::clicked,
            this, &ConnectionCard::connectClicked);
    connect(m_editButton, &QToolButton::clicked,
            this, &ConnectionCard::editClicked);
    connect(m_deleteButton, &QToolButton::clicked,
            this, &ConnectionCard::deleteClicked);
}

void ConnectionCard::setHostname(const QString &name)
{
    m_hostname = name;
    m_hostnameLabel->setText(name);
}

void ConnectionCard::setAddressPort(const QString &host, int port)
{
    m_addressPort = QStringLiteral("%1:%2").arg(host).arg(port);
    m_addressLabel->setText(m_addressPort);
}

void ConnectionCard::setResolution(int width, int height)
{
    m_resWidth = width;
    m_resHeight = height;
    m_resolutionLabel->setText(
        QStringLiteral("📺 %1×%2").arg(width).arg(height));
}

void ConnectionCard::setLastConnected(const QDateTime &time)
{
    m_lastConnected = time;
    m_timeLabel->setText(
        QStringLiteral("🕐 %1").arg(time.toString("yyyy-MM-dd HH:mm")));
}

void ConnectionCard::setOnline(bool online)
{
    m_online = online;
    m_statusIndicator->setStyleSheet(
        online
            ? "background-color: #4CAF50; border-radius: 4px;"
            : "background-color: #9E9E9E; border-radius: 4px;");
}

void ConnectionCard::retranslateUi()
{
    m_connectButton->setToolTip(tr("连接"));
    m_editButton->setToolTip(tr("修改参数"));
    m_deleteButton->setToolTip(tr("删除记录"));
}
