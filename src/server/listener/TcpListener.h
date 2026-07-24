// src/server/listener/TcpListener.h
#pragma once

#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslKey>

#include "common/error/RdError.h"
#include "common/threading/Worker.h"

class TcpServer;

/**
 * @brief TCP 监听器 — 在独立线程中管理 QTcpServer 生命周期
 *
 * 继承 Worker，消除了 ServerWorker 中间层。
 * QTcpServer 直接在 Worker 线程的 initialize() 中创建。
 */
class TcpListener : public Worker {
    Q_OBJECT

public:
    explicit TcpListener(QObject* parent = nullptr);
    ~TcpListener() override;

    Q_INVOKABLE void startListening(quint16 port, const QString& password = QString());
    Q_INVOKABLE void stopListening();

    bool isListening() const;
    quint16 port() const;
    QSslCertificate sslCertificate() const;
    QSslKey sslPrivateKey() const;

signals:
    void listening(quint16 port);
    void stopped();
    void newConnection(qintptr socketDescriptor);

protected:
    bool initialize() override;
    Q_INVOKABLE void cleanup() override;
    void processTask() override;

private slots:
    void onNewConnection(qintptr socketDescriptor);
    void onServerStopped();
    void onServerError(const RdError& error);
    void onStopTimeout();

private:
    void setupServerConnections();
    void disconnectServerSignals();

    TcpServer* m_tcpServer = nullptr;
    QTimer* m_stopTimeoutTimer = nullptr;

    mutable QMutex m_serverMutex;
    bool m_isRunning = false;
    quint16 m_currentPort = 0;

};
