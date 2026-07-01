#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QSystemTrayIcon>
#include <QtCore/QMap>
#include <QtCore/QList>
#include <QtCore/QSettings>
#include <QtCore/QDateTime>
#include "error/RdError.h"
#include "../../client/session/RemoteDesktopSession.h"  // ConnectionParams

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
class QLabel;
class QCloseEvent;
QT_END_NAMESPACE

class ConnectionDialog;
class SettingsDialog;
class ThreadManager;
class QueueManager;
class ServerManager;
class RemoteDesktopSession;
class HamburgerMenu;
class ConnectionPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
   explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    void setClientMode(bool clientMode);
    void connectToHostDirectly(const ConnectionParams& params);
    void gracefulShutdown();   // 供 main.cpp 在 std::_Exit 前手动调用

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    // 菜单和工具栏动作
    void newConnection();
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
    void onServerError(const RdError &error);
    void onClientConnected(const QString &clientId);
    void onClientDisconnected(const QString &clientId);
    void onClientAuthenticated(const QString &clientId);
    
    // 系统托盘
    void iconActivated(QSystemTrayIcon::ActivationReason reason);
    
    // 状态更新
    void updateServerStatus(const QString &message);
    void updateConnectionStatus(const QString &message);
    
private:
    void createActions();
    void createStatusBar();
    void createCentralWidget();
    QWidget *createWelcomePage();
    void createSystemTrayIcon();
    void setupConnections();

    void applyTheme();
    void toggleTheme();
    void loadSettings();
    void saveSettings();
    void retranslateUi();
    
    void showConnectionDialog();
    void cleanupConnection(const QString &connectionId);
    void updatePerformanceInfo();

#ifdef Q_OS_MACOS
    // macOS 辅助功能权限检查
    bool checkMacOSAccessibilityPermission();
    bool requestMacOSAccessibilityPermission();
#endif
    
    // UI组件
    class QWidget *m_centralWidget;
    class QLabel *m_welcomeTitleLabel;

    HamburgerMenu *m_hamburgerMenu = nullptr;
    ConnectionPanel *m_connectionPanel = nullptr;

    // 动作（仅保留托盘菜单和快捷键需要的）
    class QAction *m_exitAction;
    class QAction *m_newConnectionAction;
    class QAction *m_connectAction;
    class QAction *m_settingsAction;
    class QAction *m_restoreAction;

    // 状态栏
    class QLabel *m_connectionStatusLabel;
    class QLabel *m_serverStatusLabel;
    class QLabel *m_performanceLabel;

    // 系统托盘
    class QSystemTrayIcon *m_trayIcon;
    class QMenu *m_trayIconMenu;

    // 对话框
    ConnectionDialog *m_connectionDialog;
    SettingsDialog *m_settingsDialog;

    // 管理器
    ThreadManager *m_threadManager;
    QueueManager *m_queueManager;
    ServerManager *m_serverManager;
    QList<RemoteDesktopSession*> m_sessions;

    // 设置
    QSettings *m_settings;

    // 主题模式
    QString m_themeMode;

    // 客户端模式标志
    bool m_clientMode;

    // 停止状态标志
    bool m_isShuttingDown;
};

