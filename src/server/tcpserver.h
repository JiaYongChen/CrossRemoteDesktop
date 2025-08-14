#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QtCore/QObject>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTimer>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QThread>
#include <QtCore/QDateTime>
#include <QtGui/QPixmap>
#include "../common/core/protocol.h"
#include "inputsimulator.h"
#include "../common/core/networkconstants.h"
#include "clienthandler.h"


class TcpServer : public QTcpServer
{
    Q_OBJECT

public:
    explicit TcpServer(QObject *parent = nullptr);
    ~TcpServer();
    
    // 服务器控制
    bool startServer(quint16 port = 5900, const QHostAddress &address = QHostAddress::Any);
    void stopServer();
    void stopServer(bool synchronous);
    bool isRunning() const;
    
    // 服务器信息
    quint16 serverPort() const;
    QHostAddress serverAddress() const;
    
signals:
    void serverStarted();
    void serverStopped();
    void newClientConnection(qintptr socketDescriptor);
    void errorOccurred(const QString &error);
    
protected:
    void incomingConnection(qintptr socketDescriptor) override;
    
private:
    void setupServer();
    void cleanup();
    
    // 服务器状态
    bool m_isRunning;
    quint16 m_serverPort;
    QHostAddress m_serverAddress;
};

#endif // TCPSERVER_H