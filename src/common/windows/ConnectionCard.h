#pragma once

#include <QFrame>
#include <QDateTime>

QT_BEGIN_NAMESPACE
namespace Ui { class ConnectionCard; }
QT_END_NAMESPACE

class ConnectionCard : public QFrame
{
    Q_OBJECT

public:
    explicit ConnectionCard(QWidget *parent = nullptr);
    ~ConnectionCard() override;

    void setHostname(const QString &name);
    void setAddressPort(const QString &host, int port);
    void setResolution(int width, int height);
    void setLastConnected(const QDateTime &time);
    void setOnline(bool online);

    void retranslateUi();
    void refreshIcons();

signals:
    void connectClicked();
    void editClicked();
    void deleteClicked();

private:
    Ui::ConnectionCard *ui;

    QString m_hostname;
    QString m_addressPort;
    int m_resWidth = 0;
    int m_resHeight = 0;
    QDateTime m_lastConnected;
    bool m_online = false;
};
