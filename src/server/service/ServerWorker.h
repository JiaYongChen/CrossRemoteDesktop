#ifndef SERVERWORKER_H
#define SERVERWORKER_H

#include "../../common/core/threading/Worker.h"
#include "../../common/core/network/Protocol.h"

#include <QtCore/QObject>
#include <QtCore/QMutex>

class TcpServer;
class QTimer;

/**
 * @brief 服务器工作线程类
 *
 * 简化版本，仅负责 TcpServer 的管理和新连接的转发
 * 所有其他功能（屏幕捕获、数据处理、客户端管理等）已移至 ServerManager
 */
class ServerWorker : public Worker {
    Q_OBJECT

public:

    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit ServerWorker(QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~ServerWorker() override;

    /**
     * @brief 检查服务器是否正在运行
     * @return 如果服务器正在运行返回true，否则返回false
     */
    bool isServerRunning() const;

    /**
     * @brief 获取当前监听端口
     * @return 当前监听端口号，如果服务器未运行返回0
     */
    quint16 getCurrentPort() const;

signals:
    // 服务器状态信号
    void serverStarted(quint16 port);  ///< 服务器启动成功信号
    void serverStopped();              ///< 服务器停止信号
    void serverError(const QString& error); ///< 服务器错误信号

    // 新客户端连接信号（从TcpServer转发）
    void newClientConnection(qintptr socketDescriptor); ///< 新客户端连接信号

public slots:
    /**
     * @brief 启动服务器
     * @param port 监听端口，默认为5900
     * @return 启动成功返回true，否则返回false
     */
    bool startServer(quint16 port = 5900);

    /**
     * @brief 停止服务器
     * @param synchronous 是否同步停止，默认为false（异步停止）
     */
    void stopServer(bool synchronous = false);

protected:
    /**
     * @brief 初始化工作线程
     * @return 初始化成功返回true，否则返回false
     */
    bool initialize() override;

    /**
     * @brief 清理工作线程资源
     */
    void cleanup() override;

    /**
     * @brief 处理任务
     */
    void processTask() override;

private slots:
    // 服务器事件处理
    void onNewConnection(qintptr socketDescriptor);
    void onServerStopped();
    void onServerError(const QString& error);
    void onStopTimeout();

private:
    // 服务器管理方法
    void setupServerConnections();
    void disconnectServerSignals();

private:
    // 核心组件
    TcpServer* m_tcpServer;           ///< TCP服务器

    // 定时器
    QTimer* m_stopTimeoutTimer;       ///< 停止超时定时器

    // 服务器状态
    mutable QMutex m_serverMutex;     ///< 服务器状态互斥锁
    bool m_isServerRunning;           ///< 服务器运行状态
    quint16 m_currentPort;            ///< 当前监听端口
};

#endif // SERVERWORKER_H
