#ifndef SERVERMANAGER_H
#define SERVERMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QStringList>
#include <QtCore/QElapsedTimer>
#include <QtGui/QImage>
#include "../common/core/network/Protocol.h"

class ServerWorker;
class ThreadManager;
class IMessageCodec;

/**
 * @brief 服务器管理器类（线程代理）
 * 
 * 作为服务器工作线程的代理，提供线程安全的服务器管理接口。
 * 所有服务器操作都通过ServerWorker在独立线程中执行。
 */
class ServerManager : public QObject
{
    Q_OBJECT

public:
    explicit ServerManager(QObject *parent = nullptr);
    ~ServerManager();
    
    // 服务器控制
    bool startServer(quint16 port, const QString &password = QString());
    void stopServer();
    void gracefulShutdown();
    bool isRunning() const;
    quint16 getPort() const;
    bool isServerRunning() const;
    quint16 getCurrentPort() const;
    
    // 客户端状态查询
    int getConnectedClientCount() const;
    QStringList getConnectedClients() const;
    bool isClientConnected(const QString &clientAddress) const;
    bool hasConnectedClients() const;
    bool hasAuthenticatedClients() const;
    
    // 客户端管理
    int clientCount() const;
    QStringList connectedClients() const;
    
    // 密码管理
    void setPassword(const QString &password);
    QString password() const;
    bool verifyPassword(const QString &password) const;
    
    // 消息发送
    void sendMessageToClient(const QString &clientAddress, MessageType type, const QByteArray &data);
    
    // 客户端断开
    void disconnectClient(const QString &clientAddress);
    
signals:
    // 服务器状态信号
    void serverStarted(quint16 port);  ///< 服务器启动成功信号
    void serverStopped();              ///< 服务器停止信号
    void serverError(const QString &error); ///< 服务器错误信号
    
    // 客户端状态信号
    void clientConnected(const QString &clientAddress);    ///< 客户端连接信号
    void clientDisconnected(const QString &clientAddress); ///< 客户端断开信号
    void clientAuthenticated(const QString &clientAddress); ///< 客户端认证信号
    
private slots:
    // 从ServerWorker转发的信号处理
    void onWorkerServerStarted(quint16 port);
    void onWorkerServerStopped();
    void onWorkerServerError(const QString &error);
    void onWorkerClientConnected(const QString &clientAddress);
    void onWorkerClientDisconnected(const QString &clientAddress);
    void onWorkerClientAuthenticated(const QString &clientAddress);
    void onWorkerMessageReceived(const QString &clientAddress, MessageType type, const QByteArray &message);
    
private:
    /**
     * @brief 设置与ServerWorker的信号连接
     */
    void setupWorkerConnections();
    
    /**
     * @brief 断开与ServerWorker的信号连接
     */
    void disconnectWorkerSignals();
    
    /**
     * @brief 连接到ServerWorker的信号
     */
    void connectToServerWorker();
    
    /**
     * @brief 获取ServerWorker实例
     * @return ServerWorker指针
     */
    ServerWorker* getServerWorker() const;

private:
    ThreadManager* m_threadManager;     ///< 线程管理器
    std::atomic<bool> m_shuttingDown{false};
    mutable QMutex m_workerMutex;       ///< 工作线程互斥锁
    
    // 缓存的状态信息（用于线程安全访问）
    mutable QMutex m_stateMutex;        ///< 状态互斥锁
    bool m_isServerRunning;             ///< 服务器运行状态
    quint16 m_currentPort;              ///< 当前端口
    QString m_password;                 ///< 密码（兼容性保留）
};

#endif // SERVERMANAGER_H