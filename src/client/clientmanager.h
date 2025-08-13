#ifndef CLIENTMANAGER_H
#define CLIENTMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QList>
#include <QtCore/QHash>
#include "./managers/connectionmanager.h"
#include "clientremotewindow.h"

class SessionManager;
class QSettings;

// 单个连接的内部管理结构
struct ConnectionInstance
{
    QString connectionId;
    ConnectionManager* connectionManager;
    SessionManager* sessionManager;
    ClientRemoteWindow* remoteDesktopWindow;
    QString host;
    int port;
    
    ConnectionInstance()
        : connectionManager(nullptr)
        , sessionManager(nullptr)
        , remoteDesktopWindow(nullptr)
        , port(0)
    {}
};

class ClientManager : public QObject
{
    Q_OBJECT
    
public:
    explicit ClientManager(QObject *parent = nullptr);
    ~ClientManager();
    
    // 连接管理
    QString connectToHost(const QString &host, int port);
    void disconnectFromHost(const QString &connectionId);
    void disconnectAll();
    
    // 连接查询
    QStringList getActiveConnectionIds() const;
    int getActiveConnectionCount() const;
    bool hasActiveConnections() const;
    bool isConnected(const QString &connectionId) const;
    bool isAuthenticated(const QString &connectionId) const;
    QString getCurrentHost(const QString &connectionId) const;
    int getCurrentPort(const QString &connectionId) const;
    
    // 组件访问
    ConnectionManager* connectionManager(const QString &connectionId) const;
    SessionManager* sessionManager(const QString &connectionId) const;
    ClientRemoteWindow* remoteDesktopWindow(const QString &connectionId) const;
    
    // 窗口管理
    void createRemoteDesktopWindow(const QString &connectionId);
    void showRemoteDesktopWindow(const QString &connectionId);
    void closeRemoteDesktopWindow(const QString &connectionId);
    void closeAllRemoteDesktopWindows();
    
signals:
    void connectionEstablished(const QString &connectionId);
    void connectionClosed(const QString &connectionId);
    void connectionError(const QString &connectionId, const QString &error);
    void statusMessage(const QString &message);
    void connectionStatusChanged(const QString &connectionId);
    void connectionHistoryUpdated();
    void remoteDesktopWindowClosed(const QString &connectionId);
    
private slots:
    void onConnectionEstablished();
    void onAuthenticated();
    void onConnectionClosed();
    void onConnectionError(const QString &error);
    void onSessionStateChanged();
    void onConnectionStateChanged(ConnectionManager::ConnectionState state);
    void onScreenUpdated(const QPixmap &screen);
    void onWindowClosed();
    
private:
    void setupConnections(ConnectionInstance* instance);
    void updateConnectionStatus(const QString &connectionId);
    void cleanupResources();
    void cleanupConnection(ConnectionInstance* instance);
    QString generateConnectionId() const;
    
    ConnectionInstance* getConnectionInstance(const QString &connectionId) const;
    ConnectionInstance* findConnectionByManager(ConnectionManager* manager) const;
    ConnectionInstance* findConnectionBySessionManager(SessionManager* sessionManager) const;
    ConnectionInstance* findConnectionByWindow(ClientRemoteWindow* window) const;
    
    // 状态转换辅助方法
    ClientRemoteWindow::ConnectionState convertConnectionState(ConnectionManager::ConnectionState state) const;
    
    QHash<QString, ConnectionInstance*> m_connections;
};

#endif // CLIENTMANAGER_H