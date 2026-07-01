#ifndef CONNECTIONCARD_H
#define CONNECTIONCARD_H

#include <QFrame>
#include <QLabel>
#include <QToolButton>
#include <QDateTime>

class ConnectionCard : public QFrame
{
    Q_OBJECT

public:
    explicit ConnectionCard(QWidget *parent = nullptr);

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
    void setupUi();

    QLabel *m_statusIndicator = nullptr;
    QLabel *m_hostnameLabel = nullptr;
    QLabel *m_addressLabel = nullptr;
    QLabel *m_resolutionLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QToolButton *m_connectButton = nullptr;
    QToolButton *m_editButton = nullptr;
    QToolButton *m_deleteButton = nullptr;

    QString m_hostname;
    QString m_addressPort;
    int m_resWidth = 0;
    int m_resHeight = 0;
    QDateTime m_lastConnected;
    bool m_online = false;
};

#endif // CONNECTIONCARD_H
