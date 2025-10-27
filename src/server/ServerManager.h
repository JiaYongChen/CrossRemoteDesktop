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
class ClientHandlerWorker;
class ScreenCapture;
class DataProcessingWorker;
class QueueManager;
class QTimer;

/**
 * @brief 服务器管理器类（线程代理）
 *
 * 作为服务器工作线程的代理，提供线程安全的服务器管理接口。
 * 所有服务器操作都通过ServerWorker在独立线程中执行。
 */
class ServerManager : public QObject {
    Q_OBJECT

public:
    explicit ServerManager(QObject* parent = nullptr);
    ~ServerManager();

    // 服务器控制
    bool startServer(quint16 port, const QString& password = QString());
    void stopServer();
    void gracefulShutdown();
    bool isRunning() const;
    quint16 getPort() const;
    bool isServerRunning() const;
    quint16 getCurrentPort() const;

    // 客户端状态查询
    int getConnectedClientCount() const;
    QStringList getConnectedClients() const;
    bool isClientConnected(const QString& clientAddress) const;
    bool hasConnectedClients() const;
    bool hasAuthenticatedClients() const;

    // 客户端管理
    int clientCount() const;
    QStringList connectedClients() const;

    // 消息发送
    void sendMessageToClient(const QString& clientAddress, MessageType type, const QByteArray& data);

    // 客户端断开
    void disconnectClient(const QString& clientAddress);

signals:
    // 服务器状态信号
    void serverStarted(quint16 port);  ///< 服务器启动成功信号
    void serverStopped();              ///< 服务器停止信号
    void serverError(const QString& error); ///< 服务器错误信号

    // 客户端状态信号
    void clientConnected(const QString& clientAddress);    ///< 客户端连接信号
    void clientDisconnected(const QString& clientAddress); ///< 客户端断开信号
    void clientAuthenticated(const QString& clientAddress); ///< 客户端认证信号

private slots:
    // 从ServerWorker转发的信号处理
    void onWorkerServerStarted(quint16 port);
    void onWorkerServerStopped();
    void onWorkerServerError(const QString& error);

    // 新客户端连接处理
    void onNewClientConnection(qintptr socketDescriptor);

    // ClientHandlerWorker信号处理
    void onClientHandlerDisconnected();
    void onClientHandlerAuthenticated();
    void onClientHandlerError(const QString& error);
    void onClientHandlerMessageReceived(MessageType type, const QByteArray& data);

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
     * @return ServerWorker指针，如果不存在则返回nullptr
     */
    ServerWorker* getServerWorker() const;

    /**
     * @brief 获取DataProcessingWorker实例
     * @return DataProcessingWorker指针，如果不存在则返回nullptr
     */
    class DataProcessingWorker* getDataProcessingWorker() const;

    /**
     * @brief 停止工作线程（DataProcessingWorker和ScreenCaptureWorker）
     * 在最后一个客户端断开连接时调用，完全停止线程以节省资源
     */
    void stopWorkerThreads();

    /**
     * @brief 启动工作线程（DataProcessingWorker和ScreenCaptureWorker）
     * 在第一个客户端连接时调用，重新启动必要的工作线程
     */
    void startWorkerThreads();

    /**
     * @brief 清理断开的客户端
     */
    void cleanupDisconnectedClient();

private:
    ThreadManager* m_threadManager;     ///< 线程管理器
    std::atomic<bool> m_shuttingDown{ false };        ///< 快速停止标志（用于stopServer）
    std::atomic<bool> m_gracefulShuttingDown{ false }; ///< 优雅停止标志（用于gracefulShutdown）
    mutable QMutex m_workerMutex;       ///< 工作线程互斥锁

    // 缓存的状态信息（用于线程安全访问）
    mutable QMutex m_stateMutex;        ///< 状态互斥锁
    bool m_isServerRunning;             ///< 服务器运行状态
    quint16 m_currentPort;              ///< 当前端口
    bool m_captureStarted{ false };       ///< 屏幕捕获/数据处理是否已启动（避免重复启动）

    // 屏幕捕获和数据处理组件
    ScreenCapture* m_screenCapture;                                     ///< 屏幕捕获管理器
    DataProcessingWorker* m_dataWorker;                                 ///< 数据处理工作线程
    QueueManager* m_queueManager;                                       ///< 队列管理器

    // 客户端管理
    ClientHandlerWorker* m_currentClient;   ///< 当前客户端处理器
    QString m_currentClientThreadName;      ///< 当前客户端线程名称
    mutable QMutex m_clientMutex;           ///< 客户端互斥锁
};

#endif // SERVERMANAGER_H