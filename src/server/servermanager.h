#ifndef SERVERMANAGER_H
#define SERVERMANAGER_H

#include <QtCore/QObject>
#include <QSettings>
#include <QTimer>
#include <QPixmap>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QStringList>
#include "../common/core/protocol.h"
#include "clienthandler.h"

class TcpServer;
class ScreenCapture;
class QSystemTrayIcon;

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
    
    // 设置相关
    void setSettings(QSettings *settings);
    
    // 屏幕捕获管理
    ScreenCapture* getScreenCapture() const;
    void applyScreenCaptureSettings();
    
    // 自动启动
    void checkAutoStart();
    
    // 客户端状态查询
    bool hasConnectedClients() const;
    bool hasAuthenticatedClients() const;
    
    // 客户端管理
    int clientCount() const;
    QStringList connectedClients() const;
    
    // 客户端配置
    void setMaxClients(int maxClients);
    int maxClients() const;
    void setPassword(const QString &password);
    QString password() const;
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
    void sendScreenData(const QPixmap &frame);
    
    // 客户端管理
    void sendMessageToClient(const QString &clientAddress, MessageType type, const QByteArray &data = QByteArray());
    void sendMessageToAllClients(MessageType type, const QByteArray &data = QByteArray());
    
    // 断开连接
    void disconnectClient(const QString &clientAddress);
    void disconnectAllClients();
    
    // 连接拒绝处理
    void sendConnectionRejectionMessage(qintptr socketDescriptor, const QString &errorMessage);
    
signals:
    // 服务器状态信号
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
    void onServerStarted();
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
    void onFrameReady(const QPixmap &frame);
    
private:
    void setupServerConnections();
    void disconnectServerSignals();
    void startScreenCapture();
    void stopScreenCapture();
    
    // 客户端管理辅助方法
    ClientHandler* findClientHandler(const QString &address);
    QString generateClientId(const QString &address, quint16 port);
    void registerClientHandler(ClientHandler *handler);
    void unregisterClientHandler(const QString &clientAddress);
    
    // 网络组件
    TcpServer *m_tcpServer;
    ScreenCapture *m_screenCapture;
    
    // 设置
    QSettings *m_settings;
    
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
    QString m_password;
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
    QPixmap m_lastFrame;
};

#endif // SERVERMANAGER_H