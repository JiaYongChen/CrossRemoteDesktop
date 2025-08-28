#ifndef SERVERMANAGER_H
#define SERVERMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QStringList>
#include <QtCore/QElapsedTimer>
#include <QtGui/QImage>
#include "../common/core/protocol.h"

class TcpServer;
class ScreenCapture;
class QSystemTrayIcon;
class QSettings;
class QTimer;
class ClientHandler;
class IMessageCodec;

class ServerManager : public QObject
{
    Q_OBJECT

public:
    explicit ServerManager(QObject *parent = nullptr);
    ~ServerManager();
    
    // 服务器控制
    bool startServer();
    void stopServer(bool synchronous = false);
    bool isServerRunning() const;
    quint16 getCurrentPort() const;
    
    // 屏幕捕获管理
    ScreenCapture* getScreenCapture() const;
    void applyScreenCaptureSettings();
    
    // 客户端状态查询
    bool hasConnectedClients() const;
    bool hasAuthenticatedClients() const;
    
    // 客户端管理
    int clientCount() const;
    QStringList connectedClients() const;
    
    // 客户端配置
    void setMaxClients(int maxClients);
    int maxClients() const;
    // 阶段C：不再存储明文密码，对外提供设置口令的API，但内部保存盐+摘要
    void setPassword(const QString &password);
    QString password() const; // 为兼容旧UI，暂时保留读取接口（后续移除）；返回空或掩码
    void setAllowMultipleClients(bool allow);
    bool allowMultipleClients() const;
    
    // 统计信息
    quint64 totalBytesReceived() const;
    quint64 totalBytesSent() const;
    
    // 性能统计
    double getAverageFrameTime() const;
    double getAverageCompressionRatio() const;
    int getCurrentFrameRate() const;
    
    // 性能优化控制
    void enablePerformanceOptimization(bool enabled);
    void enableRegionDetection(bool enabled);
    void enableAdvancedEncoding(bool enabled);
    
    // 数据发送
    void sendScreenData(const QImage &frame);
    
    // 客户端管理
    void sendMessageToClient(const QString &clientId, MessageType type, const IMessageCodec &message);
    void sendMessageToAllClients(MessageType type, const IMessageCodec &message);
    
    // 断开连接
    void disconnectClient(const QString &clientId);
    void disconnectAllClients();
    
    // 连接拒绝处理
    void sendConnectionRejectionMessage(qintptr socketDescriptor, const QString &errorMessage);
    
signals:
    // 服务器状态信号
    void serverStarted(quint16 port);  // 服务器启动成功信号，包含端口号
    void serverStopped();
    void serverError(const QString &error);
    
    // 客户端连接信号
    void clientConnected(const QString &clientAddress);
    void clientDisconnected(const QString &clientAddress);
    void clientAuthenticated(const QString &clientAddress);
    
    // 客户端消息信号
    void messageReceived(const QString &clientAddress, MessageType type, const QByteArray &data);
    
    // 服务器状态消息信号
    void serverStatusMessage(const QString &message);
    
    // 客户端连接状态消息信号
    void clientStatusMessage(const QString &message);
    
private slots:
    void onServerStopped();
    void onNewConnection(qintptr socketDescriptor);
    void onClientConnected(const QString &clientAddress);
    void onClientDisconnected(const QString &clientAddress);
    void onClientAuthenticated(const QString &clientAddress);
    void onMessageReceived(const QString &clientAddress, MessageType type, const QByteArray &data);
    void onClientError(const QString &error);
    void onServerError(const QString &error);
    void onStopTimeout();
    
    // 客户端管理槽函数
    void cleanupDisconnectedClients();
    
    // 屏幕捕获相关槽函数
    void onFrameReady(const QImage &frame);
    
private:
    void setupServerConnections();
    void disconnectServerSignals();
    void startScreenCapture();
    void stopScreenCapture();
    
    // 客户端管理辅助方法
    ClientHandler* findClientHandler(const QString &clientId);
    QString generateClientId(const QString &address, quint16 port);
    void registerClientHandler(ClientHandler *handler);
    void unregisterClientHandler(const QString &clientId);
    
    // 网络组件
    TcpServer *m_tcpServer;
    ScreenCapture *m_screenCapture;
    
    // 定时器
    QTimer *m_stopTimeoutTimer;
    QTimer *m_cleanupTimer;
    
    // 状态变量
    bool m_isServerRunning;
    quint16 m_currentPort;
    
    // 客户端管理
    QHash<QString, ClientHandler*> m_clients;
    mutable QMutex m_clientsMutex;
    
    // 配置
    int m_maxClients;
    // 阶段C：改为摘要策略（PBKDF2-SHA256），不再持久保存明文
    QString m_password; // 已弃用：仅为旧UI过渡；不再对外返回明文
    QByteArray m_passwordSalt;
    QByteArray m_passwordDigest; // PBKDF2 派生的摘要
    bool m_allowMultipleClients;
    
    // 统计信息
    quint64 m_totalBytesReceived;
    quint64 m_totalBytesSent;
    
    // 性能优化相关
    bool m_performanceOptimizationEnabled;
    bool m_regionDetectionEnabled;
    bool m_advancedEncodingEnabled;
    
    // 性能统计
    mutable QElapsedTimer m_frameTimer;
    QImage m_lastFrame; // 统一为 QImage 存储
};

#endif // SERVERMANAGER_H