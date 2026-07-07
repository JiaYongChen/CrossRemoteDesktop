#include "ConnectionCard.h"
#include "ui_ConnectionCard.h"
#include "common/core/theme/IconThemeProvider.h"

ConnectionCard::ConnectionCard(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ConnectionCard)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_StyledBackground, true);
    setFixedSize(500, 110);

    // 初始状态指示灯
    ui->statusIndicator->setStyleSheet(
        "background-color: #9E9E9E; border-radius: 4px;");

    // 设置图标
    ui->connectButton->setIcon(IconThemeProvider::icon("connect"));
    ui->editButton->setIcon(IconThemeProvider::icon("save"));
    ui->deleteButton->setIcon(IconThemeProvider::icon("delete"));

    // 信号连接
    connect(ui->connectButton, &QToolButton::clicked,
            this, &ConnectionCard::connectClicked);
    connect(ui->editButton, &QToolButton::clicked,
            this, &ConnectionCard::editClicked);
    connect(ui->deleteButton, &QToolButton::clicked,
            this, &ConnectionCard::deleteClicked);
}

ConnectionCard::~ConnectionCard()
{
    delete ui;
}

void ConnectionCard::setHostname(const QString &name)
{
    ui->hostnameLabel->setText(name);
}

void ConnectionCard::setAddressPort(const QString &host, int port)
{
    const QString addr = QStringLiteral("%1:%2").arg(host).arg(port);
    ui->addressLabel->setText(addr);
}

void ConnectionCard::setResolution(int width, int height)
{
    ui->resolutionLabel->setText(
        QStringLiteral("📺 %1×%2").arg(width).arg(height));
}

void ConnectionCard::setLastConnected(const QDateTime &time)
{
    ui->timeLabel->setText(
        QStringLiteral("🕐 %1").arg(time.toString("yyyy-MM-dd HH:mm")));
}

void ConnectionCard::setOnline(bool online)
{
    ui->statusIndicator->setStyleSheet(
        online
            ? "background-color: #4CAF50; border-radius: 4px;"
            : "background-color: #9E9E9E; border-radius: 4px;");
}

void ConnectionCard::retranslateUi()
{
    ui->connectButton->setToolTip(tr("连接"));
    ui->editButton->setToolTip(tr("修改参数"));
    ui->deleteButton->setToolTip(tr("删除记录"));
}

void ConnectionCard::refreshIcons()
{
    ui->connectButton->setIcon(IconThemeProvider::icon("connect"));
    ui->editButton->setIcon(IconThemeProvider::icon("save"));
    ui->deleteButton->setIcon(IconThemeProvider::icon("delete"));
}

