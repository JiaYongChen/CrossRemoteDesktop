#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class ConnectionCard; }
QT_END_NAMESPACE

class ConnectionCard : public QWidget
{
    Q_OBJECT

public:
    explicit ConnectionCard(QWidget *parent = nullptr);
    ~ConnectionCard() override;

    void setHostname(const QString &name);
    void setAddressPort(const QString &host, int port);
    void setResolution(int width, int height);
    void setLastConnected(const QDateTime &time);

    void retranslateUi();

signals:
    void connectClicked();
    void editClicked();
    void deleteClicked();

private:
    Ui::ConnectionCard *ui = nullptr;
};
