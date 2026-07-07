#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QSystemTrayIcon>
#include <QtCore/QMap>
#include <QtCore/QList>
#include <QtCore/QSettings>
#include <QtCore/QDateTime>
#include "error/RdError.h"
#include "../../client/session/RemoteDesktopSession.h"  // ConnectionParams

class QAction;
class QMenu;
class QLabel;
class QCloseEvent;

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

class ConnectionDialog;
class SettingsDialog;
class ThreadManager;
class QueueManager;
class ServerManager;
class RemoteDesktopSession;
class NavPanel;
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
    void startServer();
    void stopServer();
    void showSettings();
    void showAbout();
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
    
    // UI组件 — 便捷指针指向 ui-> 中的同名控件，减少业务代码变更
    Ui::MainWindow *ui;

    NavPanel *m_navPanel = nullptr;
    ConnectionPanel *m_connectionPanel = nullptr;

    // 动作（托盘菜单需要）
    QAction *m_exitAction;
    QAction *m_restoreAction;

    // 状态栏
    QLabel *m_connectionStatusLabel;
    QLabel *m_serverStatusLabel;
    QLabel *m_performanceLabel;

    // 系统托盘
    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayIconMenu;

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

