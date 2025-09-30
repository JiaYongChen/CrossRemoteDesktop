#ifndef SERVERWORKER_H
#define SERVERWORKER_H

#include "../common/core/threading/Worker.h"
#include "../common/core/network/Protocol.h"
#include "dataprocessing/DataProcessing.h"
#include "dataprocessing/DataProcessingConfig.h"

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QStringList>
#include <QtCore/QElapsedTimer>
#include <QtGui/QImage>
#include <QtCore/QTimer>
#include <QtCore/QByteArray>
#include <QtCore/QBuffer>
#include <memory>

class TcpServer;
class ScreenCapture;
class ClientHandler;
class IMessageCodec;
class AdvancedCompressionManager; // 前向声明：高级压缩管理器，避免在头文件中引入实现依赖

/**
 * @brief 服务器工作线程类
 * 
 * 在独立线程中运行服务器，处理网络连接、屏幕捕获和客户端通信。
 * 继承Worker基类，实现线程安全的服务器操作。
 */
class ServerWorker : public Worker
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit ServerWorker(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~ServerWorker() override;
    
    /**
     * @brief 检查服务器是否运行
     * @return 服务器运行状态
     */
    bool isServerRunning() const;
    
    /**
     * @brief 获取当前端口
     * @return 当前监听端口
     */
    quint16 getCurrentPort() const;
    
    /**
     * @brief 获取密码（兼容性接口）
     * @return 密码字符串
     */
    QString password() const;
    
    /**
     * @brief 检查是否有已连接的客户端
     * @return 是否有客户端连接
     */
    bool hasConnectedClients() const;
    
    /**
     * @brief 检查是否有已认证的客户端
     * @return 是否有认证客户端
     */
    bool hasAuthenticatedClients() const;
    
    /**
     * @brief 获取客户端数量
     * @return 客户端连接数
     */
    int clientCount() const;
    
    /**
     * @brief 获取已连接客户端列表
     * @return 客户端地址列表
     */
    QStringList connectedClients() const;
    
    /**
     * @brief 发送消息给客户端
     * @param type 消息类型
     * @param message 消息内容
     */
    void sendMessageToClient(MessageType type, const IMessageCodec &message);
    
    /**
     * @brief 断开客户端连接
     */
    void disconnectClient();

signals:
    // 服务器状态信号
    void serverStarted(quint16 port);  ///< 服务器启动成功信号
    void serverStopped();              ///< 服务器停止信号
    void serverError(const QString &error); ///< 服务器错误信号
    
    // 客户端状态信号
    void clientConnected(const QString &clientAddress);    ///< 客户端连接信号
    void clientDisconnected(const QString &clientAddress); ///< 客户端断开信号
    void clientAuthenticated(const QString &clientAddress); ///< 客户端认证信号
    
    // 消息信号
    void messageReceived(const QString &clientAddress, MessageType type, const QByteArray &data); ///< 消息接收信号

public slots:
    /**
     * @brief 启动服务器
     * @param port 监听端口
     * @return 是否启动成功
     */
    bool startServer(quint16 port = 5900);
    
    /**
     * @brief 停止服务器
     * @param synchronous 是否同步停止
     */
    void stopServer(bool synchronous = false);
    
    /**
     * @brief 设置密码
     * @param password 服务器密码
     */
    void setPassword(const QString &password);

protected:
    /**
     * @brief 初始化工作线程
     * @return 初始化是否成功
     */
    bool initialize() override;
    
    /**
     * @brief 清理工作线程资源
     */
    void cleanup() override;
    
    /**
     * @brief 处理任务（Worker基类要求实现）
     */
    void processTask() override;

private slots:
    // TCP服务器事件处理
    void onNewConnection(qintptr socketDescriptor);
    void onServerStopped();
    void onServerError(const QString &error);
    
    // 客户端事件处理
    void onClientConnected(const QString &clientAddress);
    void onClientDisconnected(const QString &clientAddress);
    void onClientAuthenticated(const QString &clientAddress);
    void onMessageReceived(const QString &clientAddress, MessageType type, const QByteArray &data);
    void onClientError(const QString &error);
    
    // 屏幕捕获事件处理
    void onFrameReady(const QImage &frame);
    
    // 定时器事件处理
    void onStopTimeout();
    void cleanupDisconnectedClients();

private:
    /**
     * @brief 设置服务器连接
     */
    void setupServerConnections();
    
    /**
     * @brief 断开服务器信号连接
     */
    void disconnectServerSignals();
    
    /**
     * @brief 启动屏幕捕获
     */
    void startScreenCapture();
    
    /**
     * @brief 停止屏幕捕获
     */
    void stopScreenCapture();
    
    /**
     * @brief 发送屏幕数据到客户端
     * @param frame 屏幕图像帧
     */
    void sendScreenData(const QImage &frame);
    
    /**
     * @brief 发送处理后的图像数据
     * @param record 处理后的数据记录
     */
    void sendProcessedImageData(const DataRecord& record);
    
    /**
     * @brief 查找客户端处理器
     * @param clientId 客户端ID
     * @return 客户端处理器指针
     */
    ClientHandler* findClientHandler(const QString &clientId);
    
    /**
     * @brief 生成客户端ID
     * @param address 客户端地址
     * @param port 客户端端口
     * @return 客户端ID字符串
     */
    QString generateClientId(const QString &address, quint16 port);
    
    /**
     * @brief 发送连接拒绝消息
     * @param socketDescriptor 套接字描述符
     * @param errorMessage 错误消息
     */
    void sendConnectionRejectionMessage(qintptr socketDescriptor, const QString &errorMessage);

private:
    // 核心组件
    TcpServer *m_tcpServer;           ///< TCP服务器
    ScreenCapture *m_screenCapture;   ///< 屏幕捕获器
    
    // 定时器
    QTimer *m_stopTimeoutTimer;       ///< 停止超时定时器
    QTimer *m_cleanupTimer;           ///< 清理定时器
    
    // 服务器状态
    mutable QMutex m_serverMutex;     ///< 服务器状态互斥锁
    bool m_isServerRunning;           ///< 服务器运行状态
    quint16 m_currentPort;            ///< 当前监听端口
    
    // 客户端管理
    ClientHandler* m_currentClient;   ///< 当前客户端处理器
    mutable QMutex m_clientMutex;     ///< 客户端互斥锁
    
    // 认证信息
    QString m_password;               ///< 服务器密码（兼容性保留）
    QByteArray m_passwordSalt;        ///< 密码盐值
    QByteArray m_passwordDigest;      ///< 密码摘要

    // 压缩和数据处理
    std::unique_ptr<AdvancedCompressionManager> m_acm; ///< 自适应压缩管理器
    QByteArray m_prevEncodedFrameData; ///< 上一帧已经发送的编码数据（用于与当前帧做字节级差分）
    
    // 数据处理模块
    std::unique_ptr<DataProcessor> m_dataProcessor;     ///< 数据处理器
    std::unique_ptr<DataProcessingConfig> m_dataConfig; ///< 数据处理配置

 };

 #endif // SERVERWORKER_H