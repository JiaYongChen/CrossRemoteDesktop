#include "MainWindow.h"
#include "ConnectionDialog.h"
#include "SettingsDialog.h"
#include "../../server/ServerManager.h"
#include "../../client/ClientManager.h"
#include "../../server/simulator/InputSimulator.h"
#include "../core/config/UiConstants.h"
#include "../core/config/MessageConstants.h"
#include "../core/logging/LoggingCategories.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QMenu>
#include <QtGui/QAction>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QLabel>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QThread>
#include <QtGui/QIcon>
#include <QtGui/QPixmap>
#include <QtGui/QCloseEvent>
#include <QtCore/QEvent>
#include <QtGui/QContextMenuEvent>
#include <QtWidgets/QListWidgetItem>
#include <QtCore/QMessageLogger>

MainWindow::MainWindow(QWidget* parent)
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
    , m_isShuttingDown(false) {
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

    // 设置连接
    setupConnections();

    // 加载设置
    loadSettings();

    // 设置窗口属性
    setWindowTitle(tr("Qt远程桌面"));
    setMinimumSize(UIConstants::MIN_WINDOW_WIDTH, UIConstants::MIN_WINDOW_HEIGHT);
    resize(UIConstants::MAIN_WINDOW_WIDTH, UIConstants::MAIN_WINDOW_HEIGHT);
}

MainWindow::~MainWindow() {
    qCInfo(lcUI, "MainWindow::~MainWindow() - 开始析构");

    // 在析构函数中进行最后的资源清理
    // 注意：此时不应该再调用可能触发信号的方法

    // 1. 断开所有信号连接，防止在析构过程中触发信号
    if ( m_serverManager ) {
        disconnect(m_serverManager, nullptr, this, nullptr);
    }

    if ( m_clientManager ) {
        disconnect(m_clientManager, nullptr, this, nullptr);
    }

    // 2. 清理系统托盘图标
    if ( m_trayIcon ) {
        m_trayIcon->hide();
    }

    // 3. 清理对话框
    if ( m_connectionDialog ) {
        m_connectionDialog->close();
    }

    if ( m_settingsDialog ) {
        m_settingsDialog->close();
    }

    qCInfo(lcUI, "MainWindow::~MainWindow() - 析构完成");
}

void MainWindow::createActions() {
    // 文件菜单动作
    m_newConnectionAction = new QAction(tr("新建连接(&N)..."), this);
    m_newConnectionAction->setShortcuts(QKeySequence::New);
    m_newConnectionAction->setStatusTip(tr("创建新的远程连接"));
    m_newConnectionAction->setIcon(QIcon(":/icons/new_connection.svg"));

    m_exitAction = new QAction(tr("退出(&X)"), this);
    m_exitAction->setShortcuts(QKeySequence::Quit);
    m_exitAction->setStatusTip(tr("退出应用程序"));
    m_exitAction->setIcon(QIcon(":/icons/exit.svg"));

    // 连接菜单动作
    m_connectAction = new QAction(tr("连接(&C)"), this);
    m_connectAction->setShortcut(QKeySequence(tr("Ctrl+O")));
    m_connectAction->setStatusTip(tr("连接到远程主机"));
    m_connectAction->setIcon(QIcon(":/icons/connect.svg"));

    // 工具菜单动作
    m_settingsAction = new QAction(tr("设置(&S)..."), this);
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    m_settingsAction->setStatusTip(tr("配置应用程序设置"));
    m_settingsAction->setIcon(QIcon(":/icons/settings.svg"));

    // 帮助菜单动作
    m_aboutAction = new QAction(tr("关于(&A)"), this);
    m_aboutAction->setStatusTip(tr("显示应用程序的关于对话框"));
    m_aboutAction->setIcon(QIcon(":/icons/about.svg"));

    m_aboutQtAction = new QAction(tr("关于Qt(&Q)"), this);
    m_aboutQtAction->setStatusTip(tr("显示Qt库的关于对话框"));

    // 系统托盘动作
    m_minimizeAction = new QAction(tr("最小化(&N)"), this);
    m_maximizeAction = new QAction(tr("最大化(&X)"), this);
    m_restoreAction = new QAction(tr("恢复(&R)"), this);
}

void MainWindow::createMenus() {
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

void MainWindow::createToolBars() {
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

void MainWindow::createStatusBar() {
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

void MainWindow::createCentralWidget() {
    m_centralWidget = new QWidget;
    setCentralWidget(m_centralWidget);

    // 创建欢迎页面
    createWelcomeWidget();

    // 设置布局
    QHBoxLayout* layout = new QHBoxLayout;
    layout->addWidget(m_welcomeWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    m_centralWidget->setLayout(layout);
}

void MainWindow::createWelcomeWidget() {
    m_welcomeWidget = new QWidget;

    QVBoxLayout* mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(30, 30, 30, 30);
    mainLayout->setSpacing(20);

    // 欢迎标题
    QLabel* titleLabel = new QLabel(tr("欢迎使用Qt远程桌面"));
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("color: #2c3e50; margin-bottom: 10px;");

    // 描述文本
    QLabel* descLabel = new QLabel(tr("使用左侧按钮连接到远程计算机。"));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: #7f8c8d; font-size: 14px;");

    // 连接历史记录部分
    QLabel* historyLabel = new QLabel(tr("连接历史记录"));
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

void MainWindow::createSystemTrayIcon() {
    if ( !QSystemTrayIcon::isSystemTrayAvailable() ) {
        return;
    }

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setContextMenu(m_trayIconMenu);
    m_trayIcon->setIcon(QIcon(":/icons/app.svg"));
    m_trayIcon->setToolTip(tr("远程桌面"));
    m_trayIcon->show();
}

void MainWindow::setupConnections() {
    // 菜单和工具栏动作连接
    connect(m_newConnectionAction, &QAction::triggered, this, &MainWindow::newConnection);
    connect(m_connectAction, &QAction::triggered, this, &MainWindow::connectToHost);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    connect(m_aboutQtAction, &QAction::triggered, this, &MainWindow::showAboutQt);
    connect(m_exitAction, &QAction::triggered, this, &MainWindow::exitApplication);

    // UI按钮连接
    QPushButton* connectButton = findChild<QPushButton*>("connectButton");
    QPushButton* serverButton = findChild<QPushButton*>("serverButton");
    if ( connectButton ) {
        connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectToHost);
    }
    if ( serverButton ) {
        // 初始状态连接到startServer
        connect(serverButton, &QPushButton::clicked, this, &MainWindow::startServer);
        // 设置初始状态
        serverButton->setText(tr("启动服务器"));
        serverButton->setProperty("serverRunning", false);
    }

    // ServerManager信号连接
    if ( m_serverManager ) {
        connect(m_serverManager, &ServerManager::serverStarted, this, &MainWindow::onServerStarted);
        connect(m_serverManager, &ServerManager::serverStopped, this, &MainWindow::onServerStopped);
        connect(m_serverManager, &ServerManager::serverError, this, &MainWindow::onServerError);

        // 连接ServerManager的信号
        connect(m_serverManager, &ServerManager::clientConnected,
            this, &MainWindow::onClientConnected);
        connect(m_serverManager, &ServerManager::clientDisconnected,
            this, &MainWindow::onClientDisconnected);
        connect(m_serverManager, &ServerManager::clientAuthenticated,
            this, &MainWindow::onClientAuthenticated);
    }

    // ClientManager信号连接
    if ( m_clientManager ) {
        // 连接ClientManager的信号
        connect(m_clientManager, &ClientManager::connectionEstablished,
            this, &MainWindow::onConnectionEstablished);
        connect(m_clientManager, &ClientManager::allConnectionsClosed,
            this, &MainWindow::onAllConnectionsClosed);
    }

    // 系统托盘连接
    if ( m_trayIcon ) {
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);
        connect(m_minimizeAction, &QAction::triggered, this, &QWidget::hide);
        connect(m_maximizeAction, &QAction::triggered, this, &QWidget::showMaximized);
        connect(m_restoreAction, &QAction::triggered, this, &QWidget::showNormal);
    }
}

void MainWindow::loadSettings() {
    // 恢复窗口几何形状
    QWidget::restoreGeometry(m_settings->value("geometry").toByteArray());
    restoreState(m_settings->value("windowState").toByteArray());

    // 恢复分割器状态
    if ( m_mainSplitter ) {
        m_mainSplitter->restoreState(m_settings->value("splitterState").toByteArray());
    }

    // 加载历史连接记录
    loadConnectionHistory();

    // 检查是否需要自动启动服务器
    bool autoStartServer = m_settings->value("Server/autoStart", false).toBool();
    if ( autoStartServer ) {
        // 延迟启动服务器，确保UI完全初始化
        QTimer::singleShot(100, this, &MainWindow::startServer);
    }
}

void MainWindow::saveSettings() {
    // 保存窗口几何形状
    m_settings->setValue("geometry", QWidget::saveGeometry());
    m_settings->setValue("windowState", saveState());

    // 保存分割器状态
    if ( m_mainSplitter ) {
        m_settings->setValue("splitterState", m_mainSplitter->saveState());
    }

    // 保存历史连接记录
    saveConnectionHistory();

    // 统一输出保存设置日志，便于测试用例判断
    qCInfo(lcUI, "设置已保存");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    qCInfo(lcUI, "MainWindow::closeEvent() - 开始关闭窗口");

    // 防止重复关闭
    if ( m_isShuttingDown ) {
        qCInfo(lcUI, "MainWindow::closeEvent() - 已在关闭流程中，忽略重复关闭");
        event->accept();
        return;
    }

    m_isShuttingDown = true;

    // 保存设置
    saveSettings();

    // 在客户端模式下，直接退出应用程序
    if ( m_clientMode ) {
        qCInfo(lcUI, "MainWindow::closeEvent() - 客户端模式下关闭主窗口，直接退出应用程序");

        // 断开所有客户端连接
        if ( m_clientManager ) {
            m_clientManager->disconnectAll();
        }

        // 接受关闭事件
        event->accept();

        // 强制退出应用程序
        QApplication::exit(0);
        return;
    }

    // 服务器模式下执行优雅停止序列
    gracefulShutdown();

    // 统一输出最终态日志，确保测试能够稳定捕获到"服务器已停止"
    // 注意：ServerManager::gracefulShutdown()内部已具备最终态日志输出；
    // 这里在UI层再次输出同样的最终态，保证在某些日志过滤或异步退出情况下也不会丢失该关键日志。
    qInfo().noquote() << "服务器已停止";
    qCInfo(lcUI) << "服务器已停止";

    // 接受关闭事件
    event->accept();

    qCInfo(lcUI, "MainWindow::closeEvent() - 窗口关闭完成");
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if ( event->type() == QEvent::WindowStateChange ) {
        if ( isMinimized() && m_trayIcon && m_trayIcon->isVisible() ) {
            hide();
        }
    }
}

// 槽函数实现
void MainWindow::newConnection() {
    // 新建连接应该总是显示连接对话框，不管是否有选中的历史项
    showConnectionDialog();
}

void MainWindow::connectToHost() {
    // 检查是否有选中的历史连接项
    QListWidgetItem* currentItem = m_connectionList->currentItem();
    if ( currentItem ) {
        // 从UserRole中获取主机和端口信息
        QString host = currentItem->data(Qt::UserRole).toString();
        int port = currentItem->data(Qt::UserRole + 1).toInt();

        if ( !host.isEmpty() && port > 0 ) {
            // 直接连接到选中的主机，不弹出对话框
            connectToHostDirectly(host, port);
            return;
        }
    }

    // 如果没有选中项或数据无效，显示连接对话框
    showConnectionDialog();
}

void MainWindow::disconnectFromHost() {
    if ( m_clientManager && m_clientManager->hasActiveConnections() ) {
        m_clientManager->disconnectAll();
    }
}

void MainWindow::startServer() {
    if ( m_serverManager && m_serverManager->isServerRunning() ) {
        QMessageBox::information(this, MessageConstants::UI::SERVER_STATUS_TITLE, MessageConstants::UI::SERVER_ALREADY_RUNNING);
        return;
    }

    if ( !m_serverManager ) {
        QMessageBox::critical(this, MessageConstants::UI::ERROR_TITLE, MessageConstants::UI::SERVER_MANAGER_NOT_INITIALIZED);
        return;
    }

#ifdef Q_OS_MACOS
    // macOS 平台：检查辅助功能权限
    if (!checkMacOSAccessibilityPermission()) {
        QMessageBox::warning(this, tr("需要辅助功能权限"),
            tr("<p>Qt远程桌面需要<b>辅助功能权限</b>才能模拟鼠标和键盘输入。</p>"
               "<p>请按照以下步骤授予权限：</p>"
               "<ol>"
               "<li>打开<b>系统偏好设置</b></li>"
               "<li>选择<b>安全性与隐私</b></li>"
               "<li>点击<b>隐私</b>标签</li>"
               "<li>在左侧列表中选择<b>辅助功能</b></li>"
               "<li>点击左下角的锁图标解锁</li>"
               "<li>在右侧列表中勾选<b>QtRemoteDesktop</b></li>"
               "</ol>"
               "<p>授予权限后，请重启应用程序。</p>"));
        // 尝试打开系统设置
        requestMacOSAccessibilityPermission();
        return;
    }
#endif

    // 使用ServerManager启动服务器（使用默认端口，避免与系统VNC冲突）
    m_serverManager->startServer(UIConstants::DEFAULT_SERVER_PORT);
}

void MainWindow::stopServer() {
    if ( !m_serverManager || !m_serverManager->isServerRunning() ) {
        QMessageBox::information(this, MessageConstants::UI::SERVER_STATUS_TITLE, MessageConstants::UI::SERVER_NOT_RUNNING);
        return;
    }

    // 使用ServerManager停止服务器
    m_serverManager->stopServer();
}

void MainWindow::showSettings() {
    if ( !m_settingsDialog ) {
        m_settingsDialog = new SettingsDialog(this);
    }

    if ( m_settingsDialog->exec() == QDialog::Accepted ) {
        // 应用设置到ScreenCapture
        if ( m_serverManager ) {
            // m_serverManager->applyScreenCaptureSettings();
        }
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(this, tr("关于Qt远程桌面"),
        tr("<h2>Qt远程桌面 1.0</h2>"
            "<p>基于Qt 6.9.1构建的跨平台远程桌面应用程序。</p>"
            "<p>支持macOS和Windows系统之间的远程连接。</p>"));
}

void MainWindow::showAboutQt() {
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

void MainWindow::exitApplication() {
    // 断开所有客户端连接
    if ( m_clientManager ) {
        m_clientManager->disconnectAll();
    }

    // 停止服务器
    if ( m_serverManager && m_serverManager->isServerRunning() ) {
        m_serverManager->stopServer();
    }

    // 保存设置
    saveSettings();

    // 退出应用程序
    QApplication::quit();
}

void MainWindow::gracefulShutdown() {
    qCInfo(lcUI, "MainWindow::gracefulShutdown() - 开始优雅关闭");

    // 断开所有客户端连接
    if ( m_clientManager ) {
        qCInfo(lcUI, "MainWindow::gracefulShutdown() - 断开所有客户端连接");
        m_clientManager->disconnectAll();
    }

    // 停止服务器（无论当前标记是否显示正在运行，均调用优雅关闭以保证最终态日志输出与资源释放的幂等性）
    if ( m_serverManager ) {
        qCInfo(lcUI, "MainWindow::gracefulShutdown() - 停止服务器");

        // 使用gracefulShutdown方法进行同步停止（内部具备幂等保护与最终态日志输出）
        m_serverManager->gracefulShutdown();

        qCInfo(lcUI, "MainWindow::gracefulShutdown() - 服务器已正常停止");
        // 统一输出最终态日志，确保测试能够稳定捕获到“服务器已停止”
        // 使用非分类信息日志以避免分类过滤导致的遗漏
        qInfo().noquote() << "服务器已停止";
        // 同时输出分类信息日志，便于按模块检索
        qCInfo(lcUI) << "服务器已停止";
    }

    // 断开所有信号连接，防止在退出过程中触发回调
    if ( m_serverManager ) {
        disconnect(m_serverManager, nullptr, this, nullptr);
    }
    if ( m_clientManager ) {
        disconnect(m_clientManager, nullptr, this, nullptr);
    }

    qCInfo(lcUI, "MainWindow::gracefulShutdown() - 优雅关闭完成");

    // 正常退出应用程序
    QCoreApplication::quit();
}

void MainWindow::showConnectionDialog() {
    if ( !m_connectionDialog ) {
        m_connectionDialog = new ConnectionDialog(this);
    }

    // 设置连接对话框的默认端口为当前服务器端口
    if ( m_serverManager && m_serverManager->isServerRunning() ) {
        m_connectionDialog->setPort(m_serverManager->getCurrentPort());
    } else {
        // 如果服务器未运行，使用设置中的默认端口
        QSettings settings;
        int defaultPort = settings.value("Connection/defaultPort", 5900).toInt();
        m_connectionDialog->setPort(defaultPort);
    }

    if ( m_connectionDialog->exec() == QDialog::Accepted ) {
        // 处理连接请求
        QString host = m_connectionDialog->getHost();
        int port = m_connectionDialog->getPort();

        // 使用ClientManager连接到主机
        if ( m_clientManager ) {
            QString connectionId = m_clientManager->connectToHost(host, port);
        }
    }
}

void MainWindow::connectToHostDirectly(const QString& host, int port) {
    if ( m_clientManager ) {
        // 连接到主机
        QString connectionId = m_clientManager->connectToHost(host, port);
    }
}

void MainWindow::onConnectionEstablished(const QString& connectionId) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "MainWindow::onConnectionEstablished - Connection established for:" << connectionId;

    // 获取连接信息并添加到历史
    if ( m_clientManager ) {
        QString host = m_clientManager->getCurrentHost(connectionId);
        int port = m_clientManager->getCurrentPort(connectionId);
        if ( !host.isEmpty() && port > 0 ) {
            addConnectionToHistory(host, port);
        }
    }
}

void MainWindow::onServerStarted(quint16 port) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "MainWindow::onServerStarted() called with port:" << port;
    updateServerStatus(tr("服务器启动成功，端口: %1").arg(port));

    // 更新服务器按钮状态
    QPushButton* serverButton = findChild<QPushButton*>("serverButton");
    if ( serverButton ) {
        serverButton->setText(tr("停止服务器"));
        serverButton->setProperty("serverRunning", true);
        // 断开之前的连接，重新连接到stopServer
        disconnect(serverButton, &QPushButton::clicked, this, &MainWindow::startServer);
        connect(serverButton, &QPushButton::clicked, this, &MainWindow::stopServer);
    }

    // 在UI层保存成功启动的端口到设置
    if ( m_settings ) {
        m_settings->setValue("Connection/defaultPort", port);
        m_settings->setValue("server/port", port);
        m_settings->sync();  // 立即同步到磁盘
    }
}

void MainWindow::onServerStopped() {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "MainWindow::onServerStopped() called";
    updateServerStatus(tr("服务器已停止"));

    // 更新服务器按钮状态
    QPushButton* serverButton = findChild<QPushButton*>("serverButton");
    if ( serverButton ) {
        serverButton->setText(tr("启动服务器"));
        serverButton->setProperty("serverRunning", false);
        // 断开之前的连接，重新连接到startServer
        disconnect(serverButton, &QPushButton::clicked, this, &MainWindow::stopServer);
        connect(serverButton, &QPushButton::clicked, this, &MainWindow::startServer);
    }
}

void MainWindow::onServerError(const QString& error) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcApp) << "MainWindow::onServerError() called with error:" << error;
    // QMessageBox msgBox(this);
    // msgBox.setIcon(QMessageBox::Warning);
    // msgBox.setWindowTitle(tr("服务器错误"));
    // msgBox.setText(error);
    // msgBox.setStandardButtons(QMessageBox::Ok);
    // msgBox.exec();
}

void MainWindow::onClientConnected(const QString& clientId) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "MainWindow::onClientConnected() called with clientId:" << clientId;
    updateConnectionStatus(tr("客户端已连接: %1").arg(clientId));
}

void MainWindow::onClientDisconnected(const QString& clientId) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "MainWindow::onClientDisconnected() called with clientId:" << clientId;
    updateConnectionStatus(tr("客户端已断开: %1").arg(clientId));
}

void MainWindow::onClientAuthenticated(const QString& clientId) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "MainWindow::onClientAuthenticated() called with clientId:" << clientId;
    updateConnectionStatus(tr("客户端已认证: %1").arg(clientId));
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason) {
    switch ( reason ) {
        case QSystemTrayIcon::Trigger:
        case QSystemTrayIcon::DoubleClick:
            if ( isVisible() ) {
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

void MainWindow::cleanupConnection(const QString& connectionId) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcApp) << "MainWindow::cleanupConnection for:" << connectionId;
}

void MainWindow::onConnectionItemDoubleClicked() {
    // 双击直接连接到选中的历史记录，不弹出对话框
    QListWidgetItem* item = m_connectionList->currentItem();
    if ( item ) {
        QString host = item->data(Qt::UserRole).toString();
        int port = item->data(Qt::UserRole + 1).toInt();

        // 直接连接到选中的主机
        connectToHostDirectly(host, port);
    }
}

void MainWindow::addConnectionToHistory(const QString& host, int port) {
    // 更新连接列表显示
    if ( m_connectionList ) {
        // 检查是否已存在相同的连接记录
        QString connectionString = host + ":" + QString::number(port);
        bool exists = false;
        QListWidgetItem* existingItem = nullptr;

        for ( int i = 0; i < m_connectionList->count(); ++i ) {
            QListWidgetItem* item = m_connectionList->item(i);
            QString itemHost = item->data(Qt::UserRole).toString();
            int itemPort = item->data(Qt::UserRole + 1).toInt();
            if ( item && itemHost == host && itemPort == port ) {
                exists = true;
                existingItem = item;
                break;
            }
        }

        if ( !exists ) {
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

void MainWindow::removeConnectionFromHistory() {
    // 从历史记录中移除选中的连接
    QListWidgetItem* item = m_connectionList->currentItem();
    if ( item ) {
        int row = m_connectionList->currentRow();
        m_connectionList->takeItem(row);
        delete item;

        // 保存更新后的历史记录
        saveConnectionHistory();

        // 更新状态栏
        statusBar()->showMessage(tr("已删除连接记录"));
    }
}

void MainWindow::showConnectionContextMenu(const QPoint& pos) {
    // 检查是否点击在有效项目上
    QListWidgetItem* item = m_connectionList->itemAt(pos);
    if ( !item ) {
        return;
    }

    // 创建右键菜单
    QMenu contextMenu(this);

    // 添加连接动作
    QAction* connectAction = contextMenu.addAction(QIcon(":/icons/connect.svg"), tr("连接"));
    connect(connectAction, &QAction::triggered, [this, item]() {
        m_connectionList->setCurrentItem(item);
        onConnectionItemDoubleClicked();
    });

    contextMenu.addSeparator();

    // 添加删除动作
    QAction* deleteAction = contextMenu.addAction(QIcon(":/icons/delete.svg"), tr("删除"));
    connect(deleteAction, &QAction::triggered, [this, item]() {
        m_connectionList->setCurrentItem(item);

        // 确认删除
        QString connectionText = item->text();
        int ret = QMessageBox::question(this, tr("确认删除"),
            tr("确定要删除连接记录 \"%1\" 吗？").arg(connectionText),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if ( ret == QMessageBox::Yes ) {
            removeConnectionFromHistory();
        }
    });

    // 显示菜单
    contextMenu.exec(m_connectionList->mapToGlobal(pos));
}

void MainWindow::updateServerStatus(const QString& message) {
    // 检查ServerManager的连接状态
    m_serverStatusLabel->setText(message);
}

void MainWindow::updateConnectionStatus(const QString& message) {
    // 检查Client connect to server的连接状态
    m_connectionStatusLabel->setText(message);
}

void MainWindow::loadConnectionHistory() {
    if ( !m_connectionList || !m_settings ) {
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

    for ( int i = 0; i < count; ++i ) {
        QString host = hosts[i];
        int port = ports[i].toInt();
        QString connectionTime = times[i];

        if ( !host.isEmpty() && port > 0 ) {
            createConnectionListItem(host, port, connectionTime);
        }
    }

    // 自动选中最近一次连接的记录（第一个就是最新的，因为ClientManager使用prepend）
    if ( m_connectionList->count() > 0 ) {
        m_connectionList->setCurrentRow(0);
    }
}

void MainWindow::saveConnectionHistory() {
    if ( !m_connectionList || !m_settings ) {
        return;
    }

    // 保存连接历史记录到设置（使用与ClientManager相同的格式）
    m_settings->beginGroup("ConnectionHistory");

    QStringList connections;
    QStringList hosts;
    QStringList ports;
    QStringList times;

    for ( int i = 0; i < m_connectionList->count(); ++i ) {
        QListWidgetItem* item = m_connectionList->item(i);
        if ( item ) {
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

void MainWindow::setClientMode(bool clientMode) {
    m_clientMode = clientMode;

    if ( m_clientMode ) {
        // 客户端模式：不启动服务器，隐藏服务器相关UI
        setWindowTitle(tr("Qt远程桌面 - 客户端模式"));

        // 停止服务器（如果正在运行）
        if ( m_serverManager && m_serverManager->isServerRunning() ) {
            m_serverManager->stopServer();
        }

        qCInfo(lcUI, "Application set to client mode");
    } else {
        // 服务器模式：正常启动服务器
        setWindowTitle(tr("Qt远程桌面"));
        qCInfo(lcUI, "Application set to server mode");

        // 延迟启动服务器
        QTimer::singleShot(500, this, &MainWindow::startServer);
    }
}

QString MainWindow::formatConnectionText(const QString& host, int port, const QString& connectionTime) {
    return QString("主机: %1\n端口: %2\n连接时间: %3")
        .arg(host)
        .arg(port)
        .arg(connectionTime);
}

QListWidgetItem* MainWindow::createConnectionListItem(const QString& host, int port, const QString& connectionTime) {
    QListWidgetItem* item = new QListWidgetItem();
    item->setData(Qt::UserRole, host);
    item->setData(Qt::UserRole + 1, port);
    item->setData(Qt::UserRole + 2, connectionTime);

    // 创建自定义的QLabel来显示多行文本
    QLabel* label = new QLabel(formatConnectionText(host, port, connectionTime));
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
    if ( m_connectionList ) {
        m_connectionList->addItem(item);
        m_connectionList->setItemWidget(item, label);
    }

    return item;
}

void MainWindow::updateConnectionListItem(QListWidgetItem* item, const QString& host, int port, const QString& connectionTime) {
    if ( !item || !m_connectionList ) {
        return;
    }

    // 更新项目数据
    item->setData(Qt::UserRole, host);
    item->setData(Qt::UserRole + 1, port);
    item->setData(Qt::UserRole + 2, connectionTime);

    // 更新QLabel内容
    QLabel* label = qobject_cast<QLabel*>(m_connectionList->itemWidget(item));
    if ( label ) {
        label->setText(formatConnectionText(host, port, connectionTime));
    }
}

void MainWindow::onAllConnectionsClosed() {
    qCDebug(lcMainWindow) << "所有客户端连接已关闭";

    // 只有在客户端模式下才退出应用程序
    // 服务器模式下应该保持运行，等待新的客户端连接
    if ( m_clientMode ) {
        qCDebug(lcMainWindow) << "客户端模式下所有连接已关闭，退出应用程序";
        QApplication::quit();
    } else {
        qCDebug(lcMainWindow) << "服务器模式下所有连接已关闭，保持运行状态";
    }
}

#ifdef Q_OS_MACOS
bool MainWindow::checkMacOSAccessibilityPermission() {
    // 使用 InputSimulator 的静态方法检查权限
    return InputSimulator::checkAccessibilityPermission();
}

bool MainWindow::requestMacOSAccessibilityPermission() {
    // 使用 InputSimulator 的静态方法请求权限
    return InputSimulator::requestAccessibilityPermission();
}
#endif