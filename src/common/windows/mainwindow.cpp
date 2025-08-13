#include "mainwindow.h"
#include "connectiondialog.h"
#include "settingsdialog.h"
#include "../../server/servermanager.h"
#include "../../client/clientmanager.h"
#include "../core/logger.h"
#include "../core/uiconstants.h"
#include "../core/messageconstants.h"

#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QProgressBar>
#include <QSplitter>
#include <QListWidget>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QTextEdit>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QCloseEvent>
#include <QEvent>
#include <QContextMenuEvent>
#include <QListWidgetItem>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_centralWidget(nullptr)
    , m_mainSplitter(nullptr)
    , m_connectionList(nullptr)
    , m_welcomeWidget(nullptr)
    , m_trayIcon(nullptr)
    , m_connectionDialog(nullptr)
    , m_settingsDialog(nullptr)
    , m_serverManager(nullptr)
    , m_clientManager(nullptr)
    , m_settings(nullptr)
    , m_clientMode(false)
{
    // 初始化设置
    m_settings = new QSettings(this);
    
    // 创建UI组件
    createActions();
    createMenus();
    createToolBars();
    createStatusBar();
    createCentralWidget();
    createSystemTrayIcon();
    
    // 创建管理器组件
    m_serverManager = new ServerManager(this);
    m_clientManager = new ClientManager(this);
    
    // 配置服务器管理器
    m_serverManager->setSettings(m_settings);
    
    // 设置连接
    setupConnections();
    
    // 加载设置
    loadSettings();
    
    // 设置窗口属性
    setWindowTitle(tr("Qt远程桌面"));
    setMinimumSize(UIConstants::MIN_WINDOW_WIDTH, UIConstants::MIN_WINDOW_HEIGHT);
    resize(UIConstants::MAIN_WINDOW_WIDTH, UIConstants::MAIN_WINDOW_HEIGHT);
}

MainWindow::~MainWindow()
{    
    // 断开所有客户端连接（使用abort避免析构时阻塞）
    if (m_clientManager && m_clientManager->hasActiveConnections()) {
            m_clientManager->disconnectAll();
    }
    
    // 先断开ServerManager的信号连接，避免析构时信号槽问题
    if (m_serverManager) {
        disconnect(m_serverManager, nullptr, this, nullptr);
        
        // 停止服务器（同步方式，避免析构时卡死）
        if (m_serverManager->isServerRunning()) {
            m_serverManager->stopServer(true); // 强制同步停止
        }
    }
}

void MainWindow::createActions()
{
    // 文件菜单动作
    m_newConnectionAction = new QAction(tr("新建连接(&N)..."), this);
    m_newConnectionAction->setShortcuts(QKeySequence::New);
    m_newConnectionAction->setStatusTip(tr("创建新的远程连接"));
    m_newConnectionAction->setIcon(QIcon(":/icons/new_connection.png"));
    
    m_exitAction = new QAction(tr("退出(&X)"), this);
    m_exitAction->setShortcuts(QKeySequence::Quit);
    m_exitAction->setStatusTip(tr("退出应用程序"));
    m_exitAction->setIcon(QIcon(":/icons/exit.png"));
    
    // 连接菜单动作
    m_connectAction = new QAction(tr("连接(&C)"), this);
    m_connectAction->setShortcut(QKeySequence(tr("Ctrl+O")));
    m_connectAction->setStatusTip(tr("连接到远程主机"));
    m_connectAction->setIcon(QIcon(":/icons/connect.png"));
    
    // 工具菜单动作
    m_settingsAction = new QAction(tr("设置(&S)..."), this);
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    m_settingsAction->setStatusTip(tr("配置应用程序设置"));
    m_settingsAction->setIcon(QIcon(":/icons/settings.png"));
    
    // 帮助菜单动作
    m_aboutAction = new QAction(tr("关于(&A)"), this);
    m_aboutAction->setStatusTip(tr("显示应用程序的关于对话框"));
    m_aboutAction->setIcon(QIcon(":/icons/about.png"));
    
    m_aboutQtAction = new QAction(tr("关于Qt(&Q)"), this);
    m_aboutQtAction->setStatusTip(tr("显示Qt库的关于对话框"));
    
    // 系统托盘动作
    m_minimizeAction = new QAction(tr("最小化(&N)"), this);
    m_maximizeAction = new QAction(tr("最大化(&X)"), this);
    m_restoreAction = new QAction(tr("恢复(&R)"), this);
}

void MainWindow::createMenus()
{
    // 文件菜单
    m_fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    m_fileMenu->addAction(m_newConnectionAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_exitAction);
    
    // 连接菜单
    m_connectionMenu = menuBar()->addMenu(tr("连接(&C)"));
    m_connectionMenu->addAction(m_connectAction);
    m_connectionMenu->addSeparator();
    
    // 工具菜单
    m_toolsMenu = menuBar()->addMenu(tr("工具(&T)"));
    m_toolsMenu->addAction(m_settingsAction);
    
    // 帮助菜单
    m_helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    m_helpMenu->addAction(m_aboutAction);
    m_helpMenu->addAction(m_aboutQtAction);
    
    // 系统托盘菜单
    m_trayIconMenu = new QMenu(this);
    m_trayIconMenu->addAction(m_minimizeAction);
    m_trayIconMenu->addAction(m_maximizeAction);
    m_trayIconMenu->addAction(m_restoreAction);
    m_trayIconMenu->addSeparator();
    m_trayIconMenu->addAction(m_exitAction);
}

void MainWindow::createToolBars()
{
    // 主工具栏
    m_mainToolBar = addToolBar(tr("主工具栏"));
    m_mainToolBar->setObjectName("mainToolBar"); // 设置objectName避免警告
    
    // 设置工具栏位置为左侧
    addToolBar(Qt::LeftToolBarArea, m_mainToolBar);
    
    // 禁用工具栏拖拽移动
    m_mainToolBar->setMovable(false);
    
    m_mainToolBar->addAction(m_newConnectionAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_connectAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_settingsAction);
}

void MainWindow::createStatusBar()
{
    m_connectionStatusLabel = new QLabel(tr("未连接"));
    m_serverStatusLabel = new QLabel(tr("服务器已停止"));
    m_performanceLabel = new QLabel(tr("CPU: 0% | 内存: 0MB"));
    
    // 设置标签的最小宽度和样式，避免文字重叠
    m_connectionStatusLabel->setMinimumWidth(120);
    m_connectionStatusLabel->setStyleSheet("QLabel { padding: 2px 8px; border: 1px solid #ccc; border-radius: 3px; background-color: #f0f0f0; color: black; }");
    
    m_serverStatusLabel->setMinimumWidth(120);
    m_serverStatusLabel->setStyleSheet("QLabel { padding: 2px 8px; border: 1px solid #ccc; border-radius: 3px; background-color: #f0f0f0; }");
    
    m_performanceLabel->setMinimumWidth(150);
    m_performanceLabel->setStyleSheet("QLabel { padding: 2px 8px; border: 1px solid #ccc; border-radius: 3px; background-color: #f0f0f0; }");
    
    statusBar()->addWidget(m_connectionStatusLabel);
    statusBar()->addPermanentWidget(m_serverStatusLabel);
    statusBar()->addPermanentWidget(m_performanceLabel);
    
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::createCentralWidget()
{
    m_centralWidget = new QWidget;
    setCentralWidget(m_centralWidget);
    
    // 创建欢迎页面
    createWelcomeWidget();
    
    // 设置布局
    QHBoxLayout *layout = new QHBoxLayout;
    layout->addWidget(m_welcomeWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    m_centralWidget->setLayout(layout);
}

void MainWindow::createWelcomeWidget()
{
    m_welcomeWidget = new QWidget;
    
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(30, 30, 30, 30);
    mainLayout->setSpacing(20);
    
    // 欢迎标题
    QLabel *titleLabel = new QLabel(tr("欢迎使用Qt远程桌面"));
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("color: #2c3e50; margin-bottom: 10px;");
    
    // 描述文本
    QLabel *descLabel = new QLabel(tr("使用左侧按钮连接到远程计算机。"));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: #7f8c8d; font-size: 14px;");
    
    // 连接历史记录部分
    QLabel *historyLabel = new QLabel(tr("连接历史记录"));
    QFont historyFont = historyLabel->font();
    historyFont.setPointSize(16);
    historyFont.setBold(true);
    historyLabel->setFont(historyFont);
    historyLabel->setStyleSheet("color: #2c3e50; margin-top: 20px;");
    
    // 创建历史记录列表
    m_connectionList = new QListWidget;
    m_connectionList->setMaximumHeight(800);
    m_connectionList->setMinimumHeight(500);
    
    // 设置列表项样式，显示详细信息
    m_connectionList->setStyleSheet(
        "QListWidget {"
        "    background-color: #ffffff;"
        "    border: 1px solid #d0d0d0;"
        "    border-radius: 6px;"
        "    outline: none;"
        "}"
        "QListWidget::item {"
        "    color: #2c3e50;"
        "    padding: 15px 12px;"
        "    margin: 2px;"
        "    border: 1px solid transparent;"
        "    border-radius: 6px;"
        "    background-color: #e8e8e8;"
        "    font-size: 13px;"
        "    min-height: 120px;"
        "    text-align: left;"
        "}"
        "QListWidget::item:hover {"
        "    background-color: #e8f4fd;"
        "    border: 1px solid #b3d9ff;"
        "    color: #0066cc;"
        "}"
        "QListWidget::item:selected {"
        "    background-color: #0078d4;"
        "    color: white;"
        "    border: 1px solid #005a9e;"
        "    font-weight: bold;"
        "}"
        "QListWidget::item:selected:hover {"
        "    background-color: #106ebe;"
        "    border: 1px solid #004578;"
        "}"
    );
    
    // 设置文本换行模式，确保完整显示所有文本
    m_connectionList->setWordWrap(true);
    m_connectionList->setTextElideMode(Qt::ElideNone);
    
    // 启用右键菜单
    m_connectionList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_connectionList, &QListWidget::customContextMenuRequested,
            this, &MainWindow::showConnectionContextMenu);
    
    // 连接信号槽
    connect(m_connectionList, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onConnectionItemDoubleClicked);
    
    // 添加到主布局
    mainLayout->addWidget(titleLabel);
    mainLayout->addSpacing(10);
    mainLayout->addWidget(descLabel);
    mainLayout->addSpacing(30);
    mainLayout->addWidget(historyLabel);
    mainLayout->addSpacing(10);
    mainLayout->addWidget(m_connectionList);
    mainLayout->addStretch();
    
    m_welcomeWidget->setLayout(mainLayout);
}

void MainWindow::createSystemTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setContextMenu(m_trayIconMenu);
    m_trayIcon->setIcon(QIcon(":/icons/app.svg"));
    m_trayIcon->setToolTip(tr("远程桌面"));
    m_trayIcon->show();
}

void MainWindow::setupConnections()
{
    // 菜单和工具栏动作连接
    connect(m_newConnectionAction, &QAction::triggered, this, &MainWindow::newConnection);
    connect(m_connectAction, &QAction::triggered, this, &MainWindow::connectToHost);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    connect(m_aboutQtAction, &QAction::triggered, this, &MainWindow::showAboutQt);
    connect(m_exitAction, &QAction::triggered, this, &MainWindow::exitApplication);
    
    // UI按钮连接
    QPushButton *connectButton = findChild<QPushButton*>("connectButton");
    QPushButton *serverButton = findChild<QPushButton*>("serverButton");
    if (connectButton) {
        connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectToHost);
    }
    if (serverButton) {
        connect(serverButton, &QPushButton::clicked, this, &MainWindow::startServer);
    }
    
    // ServerManager信号连接
    if (m_serverManager) {
        connect(m_serverManager, &ServerManager::serverError, this, &MainWindow::onServerError);
        connect(m_serverManager, &ServerManager::serverStatusMessage, this, &MainWindow::updateServerStatus);
        connect(m_serverManager, &ServerManager::clientStatusMessage, this, &MainWindow::updateConnectionStatus);
    }
    
    // ClientManager信号连接
    if (m_clientManager) {
        // 连接ClientManager的信号
        connect(m_clientManager, &ClientManager::connectionEstablished,
                this, &MainWindow::onConnectionEstablished);
    }
    
    // 系统托盘连接
    if (m_trayIcon) {
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);
        connect(m_minimizeAction, &QAction::triggered, this, &QWidget::hide);
        connect(m_maximizeAction, &QAction::triggered, this, &QWidget::showMaximized);
        connect(m_restoreAction, &QAction::triggered, this, &QWidget::showNormal);
    }
}

void MainWindow::loadSettings()
{
    // 恢复窗口几何形状
    QWidget::restoreGeometry(m_settings->value("geometry").toByteArray());
    restoreState(m_settings->value("windowState").toByteArray());
    
    // 恢复分割器状态
    if (m_mainSplitter) {
        m_mainSplitter->restoreState(m_settings->value("splitterState").toByteArray());
    }
    
    // 加载历史连接记录
    loadConnectionHistory();
    
    // 检查是否需要自动启动服务器
    bool autoStartServer = m_settings->value("Server/autoStart", false).toBool();
    if (autoStartServer) {
        // 延迟启动服务器，确保UI完全初始化
        QTimer::singleShot(1000, this, &MainWindow::startServer);
    }
}

void MainWindow::saveSettings()
{
    // 保存窗口几何形状
    m_settings->setValue("geometry", QWidget::saveGeometry());
    m_settings->setValue("windowState", saveState());
    
    // 保存分割器状态
    if (m_mainSplitter) {
        m_settings->setValue("splitterState", m_mainSplitter->saveState());
    }
    
    // 保存历史连接记录
    saveConnectionHistory();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 在窗口关闭前立即保存设置，避免在析构过程中调用Qt方法
    if (m_settings) {
        try {
            // 保存窗口几何形状
            m_settings->setValue("geometry", QWidget::saveGeometry());
            m_settings->setValue("windowState", saveState());
            
            // 保存分割器状态
            if (m_mainSplitter) {
                m_settings->setValue("splitterState", m_mainSplitter->saveState());
            }
            
            // 保存历史连接记录
            saveConnectionHistory();
            
            m_settings->sync();
        } catch (...) {
            // 忽略保存过程中的异常
        }
    }
    
    // 立即接受关闭事件
    event->accept();
    
    // 异步处理清理工作，避免阻塞
    QTimer::singleShot(0, this, [this]() {
        // 断开所有客户端连接
        if (m_clientManager) {
        m_clientManager->disconnectAll();
        }
        
        // 如果服务器正在运行，异步停止
        if (m_serverManager) {
            m_serverManager->stopServer();
        }
        
        // 延迟退出，确保服务器完全停止
        QTimer::singleShot(200, []() {
            QApplication::quit();
        });
    });
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        if (isMinimized() && m_trayIcon && m_trayIcon->isVisible()) {
            hide();
        }
    }
}

// 槽函数实现
void MainWindow::newConnection()
{
    // 新建连接应该总是显示连接对话框，不管是否有选中的历史项
    showConnectionDialog();
}

void MainWindow::connectToHost()
{
    // 检查是否有选中的历史连接项
    QListWidgetItem *currentItem = m_connectionList->currentItem();
    if (currentItem) {
        // 从UserRole中获取主机和端口信息
        QString host = currentItem->data(Qt::UserRole).toString();
        int port = currentItem->data(Qt::UserRole + 1).toInt();
        
        if (!host.isEmpty() && port > 0) {
            // 直接连接到选中的主机，不弹出对话框
            connectToHostDirectly(host, port);
            return;
        }
    }
    
    // 如果没有选中项或数据无效，显示连接对话框
    showConnectionDialog();
}

void MainWindow::disconnectFromHost()
{
    if (m_clientManager && m_clientManager->hasActiveConnections()) {
        m_clientManager->disconnectAll();
    }
}

void MainWindow::startServer()
{
    if (m_serverManager && m_serverManager->isServerRunning()) {
        QMessageBox::information(this, MessageConstants::UI::SERVER_STATUS_TITLE, MessageConstants::UI::SERVER_ALREADY_RUNNING);
        return;
    }
    
    if (!m_serverManager) {
        QMessageBox::critical(this, MessageConstants::UI::ERROR_TITLE, MessageConstants::UI::SERVER_MANAGER_NOT_INITIALIZED);
        return;
    }
    
    // 使用ServerManager启动服务器
    m_serverManager->startServer();
}

void MainWindow::stopServer()
{
    if (!m_serverManager || !m_serverManager->isServerRunning()) {
        QMessageBox::information(this, MessageConstants::UI::SERVER_STATUS_TITLE, MessageConstants::UI::SERVER_NOT_RUNNING);
        return;
    }
    
    // 使用ServerManager停止服务器
    m_serverManager->stopServer();
}

void MainWindow::showSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(this);
    }
    
    if (m_settingsDialog->exec() == QDialog::Accepted) {
        // 应用设置到ScreenCapture
        if (m_serverManager) {
            m_serverManager->applyScreenCaptureSettings();
        }
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("关于Qt远程桌面"),
                      tr("<h2>Qt远程桌面 1.0</h2>"
                         "<p>基于Qt 6.9.1构建的跨平台远程桌面应用程序。</p>"
                         "<p>支持macOS和Windows系统之间的远程连接。</p>"));
}

void MainWindow::showAboutQt()
{
    QMessageBox::about(this, tr("关于Qt"),
                      tr("<h2>关于Qt</h2>"
                         "<p>本程序使用Qt版本6.9.1。</p>"
                         "<p>Qt是一个用于跨平台应用程序开发的C++工具包。</p>"
                         "<p>Qt为所有主要桌面操作系统提供单一源代码的可移植性。它也可用于嵌入式Linux和其他嵌入式及移动操作系统。</p>"
                         "<p>Qt可在多种许可选项下使用，旨在满足我们各种用户的需求。</p>"
                         "<p>根据我们的商业许可协议许可的Qt适用于开发专有/商业软件，您不希望与第三方共享任何源代码或无法遵守GNU(L)GPL条款。</p>"
                         "<p>根据GNU(L)GPL许可的Qt适用于Qt应用程序的开发，前提是您可以遵守相应许可证的条款和条件。</p>"
                         "<p>版权所有 (C) Qt公司有限公司及其他贡献者。</p>"
                         "<p>Qt和Qt标志是Qt公司有限公司的商标。</p>"
                         "<p>Qt是Qt公司有限公司开发的开源项目产品。</p>"));
}

void MainWindow::exitApplication()
{    
    // 断开所有客户端连接
    if (m_clientManager) {
        m_clientManager->disconnectAll();
    }
    
    // 停止服务器
    if (m_serverManager && m_serverManager->isServerRunning()) {
        m_serverManager->stopServer();
    }
    
    // 保存设置
    saveSettings();
    
    // 退出应用程序
    QApplication::quit();
}

void MainWindow::showConnectionDialog()
{
    if (!m_connectionDialog) {
        m_connectionDialog = new ConnectionDialog(this);
    }
    
    // 设置连接对话框的默认端口为当前服务器端口
    if (m_serverManager && m_serverManager->isServerRunning()) {
        m_connectionDialog->setPort(m_serverManager->getCurrentPort());
    } else {
        // 如果服务器未运行，使用设置中的默认端口
        QSettings settings;
        int defaultPort = settings.value("Connection/defaultPort", 5900).toInt();
        m_connectionDialog->setPort(defaultPort);
    }
    
    if (m_connectionDialog->exec() == QDialog::Accepted) {
        // 处理连接请求
        QString host = m_connectionDialog->getHost();
        int port = m_connectionDialog->getPort();
        
        // 使用ClientManager连接到主机
        if (m_clientManager) {
            QString connectionId = m_clientManager->connectToHost(host, port);
        }
    }
}

void MainWindow::connectToHostDirectly(const QString &host, int port)
{
    if (m_clientManager) {
        // 连接到主机
        QString connectionId = m_clientManager->connectToHost(host, port);
    }
}

void MainWindow::onConnectionEstablished(const QString &connectionId)
{
    qDebug() << "MainWindow::onConnectionEstablished - Connection established for:" << connectionId;
    
    // 获取连接信息并添加到历史
    if (m_clientManager) {
        QString host = m_clientManager->getCurrentHost(connectionId);
        int port = m_clientManager->getCurrentPort(connectionId);
        if (!host.isEmpty() && port > 0) {
            addConnectionToHistory(host, port);
        }
    }
}

void MainWindow::onServerError(const QString &error)
{
    qDebug() << "MainWindow::onServerError() called with error:" << error;
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(tr("服务器错误"));
    msgBox.setText(error);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:
        if (isVisible()) {
            hide();
        } else {
            show();
            raise();
            activateWindow();
        }
        break;
    default:
        break;
    }
}

void MainWindow::cleanupConnection(const QString &connectionId)
{
    qDebug() << "MainWindow::cleanupConnection for:" << connectionId;
}

void MainWindow::onConnectionItemDoubleClicked()
{
    // 双击直接连接到选中的历史记录，不弹出对话框
    QListWidgetItem *item = m_connectionList->currentItem();
    if (item) {
        QString host = item->data(Qt::UserRole).toString();
        int port = item->data(Qt::UserRole + 1).toInt();
        
        // 直接连接到选中的主机
        connectToHostDirectly(host, port);
    }
}

void MainWindow::addConnectionToHistory(const QString &host, int port)
{
    // 更新连接列表显示
    if (m_connectionList) {
        // 检查是否已存在相同的连接记录
        QString connectionString = host + ":" + QString::number(port);
        bool exists = false;
        QListWidgetItem *existingItem = nullptr;
        
        for (int i = 0; i < m_connectionList->count(); ++i) {
            QListWidgetItem *item = m_connectionList->item(i);
            QString itemHost = item->data(Qt::UserRole).toString();
            int itemPort = item->data(Qt::UserRole + 1).toInt();
            if (item && itemHost == host && itemPort == port) {
                exists = true;
                existingItem = item;
                break;
            }
        }
        
        if (!exists) {
            // 添加新项目
            QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            createConnectionListItem(host, port, currentTime);
            
            // 保存到历史记录
            saveConnectionHistory();
        } else {
            // 如果连接已存在，更新连接时间
            QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            updateConnectionListItem(existingItem, host, port, currentTime);
            
            // 保存到历史记录
            saveConnectionHistory();
        }
    }
}

void MainWindow::removeConnectionFromHistory()
{
    // 从历史记录中移除选中的连接
    QListWidgetItem *item = m_connectionList->currentItem();
    if (item) {
        int row = m_connectionList->currentRow();
        m_connectionList->takeItem(row);
        delete item;
        
        // 保存更新后的历史记录
        saveConnectionHistory();
        
        // 更新状态栏
        statusBar()->showMessage(tr("已删除连接记录"));
    }
}

void MainWindow::showConnectionContextMenu(const QPoint &pos)
{
    // 检查是否点击在有效项目上
    QListWidgetItem *item = m_connectionList->itemAt(pos);
    if (!item) {
        return;
    }
    
    // 创建右键菜单
    QMenu contextMenu(this);
    
    // 添加连接动作
    QAction *connectAction = contextMenu.addAction(QIcon(":/icons/connect.png"), tr("连接"));
    connect(connectAction, &QAction::triggered, [this, item]() {
        m_connectionList->setCurrentItem(item);
        onConnectionItemDoubleClicked();
    });
    
    contextMenu.addSeparator();
    
    // 添加删除动作
    QAction *deleteAction = contextMenu.addAction(QIcon(":/icons/delete.png"), tr("删除"));
    connect(deleteAction, &QAction::triggered, [this, item]() {
        m_connectionList->setCurrentItem(item);
        
        // 确认删除
        QString connectionText = item->text();
        int ret = QMessageBox::question(this, tr("确认删除"),
                                       tr("确定要删除连接记录 \"%1\" 吗？").arg(connectionText),
                                       QMessageBox::Yes | QMessageBox::No,
                                       QMessageBox::No);
        
        if (ret == QMessageBox::Yes) {
            removeConnectionFromHistory();
        }
    });
    
    // 显示菜单
    contextMenu.exec(m_connectionList->mapToGlobal(pos));
}

void MainWindow::updateServerStatus(const QString &message)
{
    // 检查ServerManager的连接状态
    m_serverStatusLabel->setText(message);
}

void MainWindow::updateConnectionStatus(const QString &message)
{
    // 检查Client connect to server的连接状态
    m_connectionStatusLabel->setText(message);
}

void MainWindow::loadConnectionHistory()
{
    if (!m_connectionList || !m_settings) {
        return;
    }
    
    // 清空现有列表
    m_connectionList->clear();
    
    // 从设置中读取历史连接记录（使用与ClientManager相同的格式）
    m_settings->beginGroup("ConnectionHistory");
    
    QStringList hosts = m_settings->value("hosts").toStringList();
    QStringList ports = m_settings->value("ports").toStringList();
    QStringList times = m_settings->value("times").toStringList();
    
    m_settings->endGroup();
    
    // 确保所有列表长度一致
    int count = qMin(qMin(hosts.size(), ports.size()), times.size());
    
    for (int i = 0; i < count; ++i) {
        QString host = hosts[i];
        int port = ports[i].toInt();
        QString connectionTime = times[i];
        
        if (!host.isEmpty() && port > 0) {
            createConnectionListItem(host, port, connectionTime);
        }
    }
    
    // 自动选中最近一次连接的记录（第一个就是最新的，因为ClientManager使用prepend）
    if (m_connectionList->count() > 0) {
        m_connectionList->setCurrentRow(0);
    }
}

void MainWindow::saveConnectionHistory()
{
    if (!m_connectionList || !m_settings) {
        return;
    }
    
    // 保存连接历史记录到设置（使用与ClientManager相同的格式）
    m_settings->beginGroup("ConnectionHistory");
    
    QStringList connections;
    QStringList hosts;
    QStringList ports;
    QStringList times;
    
    for (int i = 0; i < m_connectionList->count(); ++i) {
        QListWidgetItem *item = m_connectionList->item(i);
        if (item) {
            QString host = item->data(Qt::UserRole).toString();
            int port = item->data(Qt::UserRole + 1).toInt();
            QString time = item->data(Qt::UserRole + 2).toString();
            
            connections.append(QString("%1:%2").arg(host).arg(port));
            hosts.append(host);
            ports.append(QString::number(port));
            times.append(time);
        }
    }
    
    m_settings->setValue("connections", connections);
    m_settings->setValue("hosts", hosts);
    m_settings->setValue("ports", ports);
    m_settings->setValue("times", times);
    
    m_settings->endGroup();
    m_settings->sync();
}

void MainWindow::setClientMode(bool clientMode)
{
    m_clientMode = clientMode;
    
    if (m_clientMode) {
        // 客户端模式：不启动服务器，隐藏服务器相关UI
        setWindowTitle(tr("Qt远程桌面 - 客户端模式"));
        
        // 停止服务器（如果正在运行）
        if (m_serverManager && m_serverManager->isServerRunning()) {
            m_serverManager->stopServer();
        }
        
        LOG_INFO("Application set to client mode");
    } else {
        // 服务器模式：正常启动服务器
        setWindowTitle(tr("Qt远程桌面"));
        LOG_INFO("Application set to server mode");
        
        // 延迟启动服务器
        QTimer::singleShot(1000, this, &MainWindow::startServer);
    }
}

QString MainWindow::formatConnectionText(const QString &host, int port, const QString &connectionTime)
{
    return QString("主机: %1\n端口: %2\n连接时间: %3")
           .arg(host)
           .arg(port)
           .arg(connectionTime);
}

QListWidgetItem* MainWindow::createConnectionListItem(const QString &host, int port, const QString &connectionTime)
{
    QListWidgetItem *item = new QListWidgetItem();
    item->setData(Qt::UserRole, host);
    item->setData(Qt::UserRole + 1, port);
    item->setData(Qt::UserRole + 2, connectionTime);
    
    // 创建自定义的QLabel来显示多行文本
    QLabel *label = new QLabel(formatConnectionText(host, port, connectionTime));
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setStyleSheet(
        "QLabel {"
        "    color: #2c3e50;"
        "    padding: 15px 12px;"
        "    background-color: transparent;"
        "    font-size: 13px;"
        "}"
    );
    
    // 设置项目高度以适应多行文本
    item->setSizeHint(QSize(0, 120));
    
    // 将QLabel关联到列表项
    if (m_connectionList) {
        m_connectionList->addItem(item);
        m_connectionList->setItemWidget(item, label);
    }
    
    return item;
}

void MainWindow::updateConnectionListItem(QListWidgetItem *item, const QString &host, int port, const QString &connectionTime)
{
    if (!item || !m_connectionList) {
        return;
    }
    
    // 更新项目数据
    item->setData(Qt::UserRole, host);
    item->setData(Qt::UserRole + 1, port);
    item->setData(Qt::UserRole + 2, connectionTime);
    
    // 更新QLabel内容
    QLabel *label = qobject_cast<QLabel*>(m_connectionList->itemWidget(item));
    if (label) {
        label->setText(formatConnectionText(host, port, connectionTime));
    }
}