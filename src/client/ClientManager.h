#ifndef CLIENTMANAGER_H
#define CLIENTMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QHash>
#include <QtCore/QStringList>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtGui/QPixmap>
#include "./network/ConnectionManager.h"
#include "./window/ClientRemoteWindow.h"

class SessionManager;
class QSettings;
class ThreadManager;
class QTimer;

/**
 * @brief 连接实例结构体 - 管理单个远程桌面连接的所有组件
 *
 * 该结构体封装了一个远程桌面连接所需的所有组件，包括
 * 会话管理器和远程桌面窗口。使用QPointer确保对象安全访问。
 */
struct ConnectionInstance {
    // 超时配置常量
    static constexpr int THREAD_QUIT_TIMEOUT_MS = 3000;     ///< 线程优雅退出超时（毫秒）
    static constexpr int THREAD_TERMINATE_TIMEOUT_MS = 1000; ///< 线程强制终止后等待超时（毫秒）
    static constexpr int DISCONNECT_TIMEOUT_MS = 5000;       ///< 断开连接超时（毫秒）
    
    QString connectionId;                           ///< 连接的唯一标识符
    QPointer<SessionManager> sessionManager;       ///< 会话和远程桌面数据管理器
    QPointer<ClientRemoteWindow> remoteDesktopWindow; ///< 远程桌面窗口
    QThread* instanceThread = nullptr;               ///< SessionManager 所在的独立线程
    bool isBeingDeleted = false;                    ///< 标志位：防止双重删除

    /**
     * @brief 默认构造函数
     */
    ConnectionInstance() = default;

    /**
     * @brief 带连接ID的构造函数
     * @param id 连接标识符
     */
    explicit ConnectionInstance(const QString& id) : connectionId(id) {}

    /**
     * @brief 析构函数 - 确保资源正确清理
     */
    ~ConnectionInstance();

    /**
     * @brief 检查连接实例是否有效
     * @return 如果所有必要组件都存在且有效则返回true
     */
    bool isValid() const;

    /**
     * @brief 获取当前连接状态
     * @return 连接状态字符串
     */
    QString getConnectionState() const;

    /**
     * @brief 获取连接的主机地址
     * @return 主机地址，如果会话管理器无效则返回空字符串
     */
    QString getHost() const;

    /**
     * @brief 获取连接的端口号
     * @return 端口号，如果会话管理器无效则返回0
     */
    int getPort() const;

    /**
     * @brief 检查是否已连接
     * @return 如果已连接则返回true
     */
    bool isConnected() const;

    /**
     * @brief 检查是否已认证
     * @return 如果已认证则返回true
     */
    bool isAuthenticated() const;
};

class ClientManager : public QObject {
    Q_OBJECT

public:
    explicit ClientManager(QObject* parent = nullptr);
    ~ClientManager();

    // 连接管理
    QString connectToHost(const QString& host, int port);
    void disconnectFromHost(const QString& connectionId);
    void disconnectAll();

    // 连接查询
    QStringList getActiveConnectionIds() const;
    int getActiveConnectionCount() const;
    bool hasActiveConnections() const;
    bool isConnected(const QString& connectionId) const;
    bool isAuthenticated(const QString& connectionId) const;
    QString getCurrentHost(const QString& connectionId) const;
    int getCurrentPort(const QString& connectionId) const;

    // 组件访问
    SessionManager* sessionManager(const QString& connectionId) const;
    ClientRemoteWindow* remoteDesktopWindow(const QString& connectionId) const;

    // 窗口管理
    ClientRemoteWindow* createRemoteDesktopWindow(const SessionManager* sessionManager);
    void closeAllRemoteDesktopWindows();

signals:
    void connectionEstablished(const QString& connectionId);
    void allConnectionsClosed();

private slots:
    void onConnectionEstablished();
    void onAuthenticated();
    void onConnectionClosed();
    void onConnectionError(const QString& error);
    void onWindowClosed();
    void updateScreens();  // 定时器触发，从队列中拉取图片并刷新

private:
    ConnectionInstance* getConnectionInstance(const QString& connectionId) const;
    void cleanupResources();
    void cleanupConnection(ConnectionInstance* instance);
    QString generateConnectionId() const;

private:
    QHash<QString, ConnectionInstance*> m_connections;
    QTimer* m_screenUpdateTimer;  // 定时器，用于定期从队列拉取图片
};

#endif // CLIENTMANAGER_H