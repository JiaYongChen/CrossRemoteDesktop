#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QStatusBar>
#include <QtGui/QAction>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QStackedWidget>
#include <QtCore/QTimer>
#include <QtWidgets/QSystemTrayIcon>
#include <QtGui/QCloseEvent>
#include <QtCore/QSettings>
#include <QtCore/QList>
#include <QtCore/QMap>

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
class QLabel;
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
    
    // 服务器相关槽函数
    void onServerError(const QString &error);
    
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
    QWidget *m_centralWidget;
    QSplitter *m_mainSplitter;
    QListWidget *m_connectionList;
    QWidget *m_welcomeWidget;
    
    // 菜单
    QMenu *m_fileMenu;
    QMenu *m_connectionMenu;
    QMenu *m_toolsMenu;
    QMenu *m_helpMenu;
    QMenu *m_trayIconMenu;
    
    // 工具栏
    QToolBar *m_mainToolBar;
    QToolBar *m_connectionToolBar;
    
    // 动作
    QAction *m_newConnectionAction;
    QAction *m_connectAction;
    QAction *m_settingsAction;
    QAction *m_exitAction;
    QAction *m_aboutAction;
    QAction *m_aboutQtAction;
    QAction *m_minimizeAction;
    QAction *m_maximizeAction;
    QAction *m_restoreAction;
    
    // 状态栏
    QLabel *m_connectionStatusLabel;
    QLabel *m_serverStatusLabel;
    QLabel *m_performanceLabel;
    
    // 系统托盘
    QSystemTrayIcon *m_trayIcon;
    
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
};

#endif // MAINWINDOW_H