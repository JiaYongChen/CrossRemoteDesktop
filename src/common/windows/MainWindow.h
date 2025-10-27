#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QListWidget>
#include <QtCore/QMap>
#include <QtCore/QList>
#include <QtCore/QSettings>

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
class QLabel;
class QCloseEvent;
QT_END_NAMESPACE

class ConnectionDialog;
class SettingsDialog;
class ServerManager;
class ClientManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
   explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    void setClientMode(bool clientMode);
    void connectToHostDirectly(const QString &host, int port);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    // 菜单和工具栏动作
    void newConnection();
    void connectToHost();
    void disconnectFromHost();
    void startServer();
    void stopServer();
    void showSettings();
    void showAbout();
    void showAboutQt();
    void exitApplication();
    
    // 连接相关槽函数
    void onConnectionEstablished(const QString &connectionId);
    void onAllConnectionsClosed();       // 处理所有客户端连接关闭
    
    // 服务器相关槽函数
    void onServerStarted(quint16 port);  // 处理服务器启动成功
    void onServerStopped();              // 处理服务器停止
    void onServerError(const QString &error);
    void onClientConnected(const QString &clientId);
    void onClientDisconnected(const QString &clientId);
    void onClientAuthenticated(const QString &clientId);
    
    // 系统托盘
    void iconActivated(QSystemTrayIcon::ActivationReason reason);
    
    // 连接列表管理
    void onConnectionItemDoubleClicked();
    void addConnectionToHistory(const QString &host, int port);
    void removeConnectionFromHistory();
    void showConnectionContextMenu(const QPoint &pos);
    
    // 状态更新
    void updateServerStatus(const QString &message);
    void updateConnectionStatus(const QString &message);
    
    // 辅助函数
    QListWidgetItem* createConnectionListItem(const QString &host, int port, const QString &connectionTime);
    void updateConnectionListItem(QListWidgetItem *item, const QString &host, int port, const QString &connectionTime);
    QString formatConnectionText(const QString &host, int port, const QString &connectionTime);
    
    // 优雅停止相关
    void gracefulShutdown();

private:
    void createActions();
    void createMenus();
    void createToolBars();
    void createStatusBar();
    void createCentralWidget();
    void createWelcomeWidget();
    void createSystemTrayIcon();
    void setupConnections();
    
    void loadSettings();
    void saveSettings();
    void loadConnectionHistory();
    void saveConnectionHistory();
    
    void showConnectionDialog();
    void cleanupConnection(const QString &connectionId);
    
    // UI组件
    class QWidget *m_centralWidget;
    class QSplitter *m_mainSplitter;
    class QListWidget *m_connectionList;
    class QWidget *m_welcomeWidget;
    
    // 菜单
    class QMenu *m_fileMenu;
    class QMenu *m_connectionMenu;
    class QMenu *m_toolsMenu;
    class QMenu *m_helpMenu;
    class QMenu *m_trayIconMenu;
    
    // 工具栏
    class QToolBar *m_mainToolBar;
    class QToolBar *m_connectionToolBar;
    
    // 动作
    class QAction *m_newConnectionAction;
    class QAction *m_connectAction;
    class QAction *m_settingsAction;
    class QAction *m_exitAction;
    class QAction *m_aboutAction;
    class QAction *m_aboutQtAction;
    class QAction *m_minimizeAction;
    class QAction *m_maximizeAction;
    class QAction *m_restoreAction;
    
    // 状态栏
    class QLabel *m_connectionStatusLabel;
    class QLabel *m_serverStatusLabel;
    class QLabel *m_performanceLabel;
    
    // 系统托盘
    class QSystemTrayIcon *m_trayIcon;
    
    // 对话框
    ConnectionDialog *m_connectionDialog;
    SettingsDialog *m_settingsDialog;
    
    // 管理器
    ServerManager *m_serverManager;
    ClientManager *m_clientManager;
    
    // 设置
    QSettings *m_settings;
    
    // 连接历史记录
    QMap<QString, QVariant> m_connectionHistory;
    
    // 客户端模式标志
    bool m_clientMode;
    
    // 停止状态标志
    bool m_isShuttingDown;
};

#endif // MAINWINDOW_H