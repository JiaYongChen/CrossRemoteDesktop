#pragma once

#include <QtNetwork/QHostAddress>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslKey>
#include <QtNetwork/QTcpServer>

#include "common/error/RdError.h"

class SettingsManager;

class TcpServer : public QTcpServer {
    Q_OBJECT

public:
    explicit TcpServer(QObject* parent = nullptr, SettingsManager* settings = nullptr);
    ~TcpServer();

    // 服务器控制
    bool startServer(quint16 port = 5900, const QHostAddress& address = QHostAddress::Any);
    void stopServer();
    void stopServer(bool synchronous);
    bool isRunning() const;

    // 服务器信息
    quint16 serverPort() const;
    QHostAddress serverAddress() const;

    // TLS证书访问（供ClientHandlerWorker使用）
    QSslCertificate sslCertificate() const { return m_sslCertificate; }
    QSslKey sslPrivateKey() const { return m_sslPrivateKey; }

signals:
    void serverStopped();
    void newClientConnection(qintptr socketDescriptor);
    void errorOccurred(const RdError& error);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    bool generateSelfSignedCertificate();

    // 服务器状态
    bool m_isRunning;
    quint16 m_serverPort;
    QHostAddress m_serverAddress;

    // TLS证书和密钥
    QSslCertificate m_sslCertificate;
    QSslKey m_sslPrivateKey;

    SettingsManager* m_settings = nullptr;   ///< 证书持久化入口（可为空 → 仅生成不落盘）

    /// 从 SettingsManager 加载既有证书/私钥 PEM；成功置成员并返回 true
    [[nodiscard]] bool loadPersistedCertificate();
    /// 将当前证书/私钥 PEM 写入 SettingsManager
    void persistCertificate();
};

