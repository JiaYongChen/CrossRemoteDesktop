// MainWindowLayout.cpp — UI 构建方法实现
// 从 MainWindow.cpp 分离，保持文件聚焦于业务逻辑

#include "MainWindow.h"
#include "../core/config/UiConstants.h"

#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtGui/QAction>
#include <QtGui/QIcon>
#include <QtGui/QKeySequence>

void MainWindow::createActions() {
    m_newConnectionAction = new QAction(tr("新建连接(&N)..."), this);
    m_newConnectionAction->setShortcuts(QKeySequence::New);
    m_newConnectionAction->setStatusTip(tr("创建新的远程连接"));
    m_newConnectionAction->setIcon(QIcon(":/icons/new_connection.svg"));

    m_exitAction = new QAction(tr("退出(&X)"), this);
    m_exitAction->setShortcuts(QKeySequence::Quit);
    m_exitAction->setStatusTip(tr("退出应用程序"));
    m_exitAction->setIcon(QIcon(":/icons/exit.svg"));

    m_connectAction = new QAction(tr("连接(&C)"), this);
    m_connectAction->setShortcut(QKeySequence(tr("Ctrl+O")));
    m_connectAction->setStatusTip(tr("连接到远程主机"));
    m_connectAction->setIcon(QIcon(":/icons/connect.svg"));

    m_settingsAction = new QAction(tr("设置(&S)..."), this);
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    m_settingsAction->setStatusTip(tr("配置应用程序设置"));
    m_settingsAction->setIcon(QIcon(":/icons/settings.svg"));

    m_aboutAction = new QAction(tr("关于(&A)"), this);
    m_aboutAction->setStatusTip(tr("显示应用程序的关于对话框"));
    m_aboutAction->setIcon(QIcon(":/icons/about.svg"));

    m_aboutQtAction = new QAction(tr("关于Qt(&Q)"), this);
    m_aboutQtAction->setStatusTip(tr("显示Qt库的关于对话框"));

    m_minimizeAction = new QAction(tr("最小化(&N)"), this);
    m_maximizeAction = new QAction(tr("最大化(&X)"), this);
    m_restoreAction = new QAction(tr("恢复(&R)"), this);
}

void MainWindow::createMenus() {
    m_fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    m_fileMenu->addAction(m_newConnectionAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_exitAction);

    m_connectionMenu = menuBar()->addMenu(tr("连接(&C)"));
    m_connectionMenu->addAction(m_connectAction);
    m_connectionMenu->addSeparator();

    m_toolsMenu = menuBar()->addMenu(tr("工具(&T)"));
    m_toolsMenu->addAction(m_settingsAction);

    m_helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    m_helpMenu->addAction(m_aboutAction);
    m_helpMenu->addAction(m_aboutQtAction);

    m_trayIconMenu = new QMenu(this);
    m_trayIconMenu->addAction(m_minimizeAction);
    m_trayIconMenu->addAction(m_maximizeAction);
    m_trayIconMenu->addAction(m_restoreAction);
    m_trayIconMenu->addSeparator();
    m_trayIconMenu->addAction(m_exitAction);
}

void MainWindow::createToolBars() {
    m_mainToolBar = addToolBar(tr("主工具栏"));
    m_mainToolBar->setObjectName("mainToolBar");
    addToolBar(Qt::LeftToolBarArea, m_mainToolBar);
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
    createWelcomeWidget();

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

    m_welcomeTitleLabel = new QLabel(tr("欢迎使用Qt远程桌面"));
    QFont titleFont = m_welcomeTitleLabel->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    m_welcomeTitleLabel->setFont(titleFont);
    m_welcomeTitleLabel->setAlignment(Qt::AlignCenter);
    m_welcomeTitleLabel->setStyleSheet("color: #2c3e50; margin-bottom: 10px;");

    m_welcomeDescLabel = new QLabel(tr("使用左侧按钮连接到远程计算机。"));
    m_welcomeDescLabel->setAlignment(Qt::AlignCenter);
    m_welcomeDescLabel->setWordWrap(true);
    m_welcomeDescLabel->setStyleSheet("color: #7f8c8d; font-size: 14px;");

    m_welcomeHistoryLabel = new QLabel(tr("连接历史记录"));
    QFont historyFont = m_welcomeHistoryLabel->font();
    historyFont.setPointSize(16);
    historyFont.setBold(true);
    m_welcomeHistoryLabel->setFont(historyFont);
    m_welcomeHistoryLabel->setStyleSheet("color: #2c3e50; margin-top: 20px;");

    m_connectionList = new QListWidget;
    m_connectionList->setMaximumHeight(800);
    m_connectionList->setMinimumHeight(500);

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

    m_connectionList->setWordWrap(true);
    m_connectionList->setTextElideMode(Qt::ElideNone);
    m_connectionList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_connectionList, &QListWidget::customContextMenuRequested,
        this, &MainWindow::showConnectionContextMenu);
    connect(m_connectionList, &QListWidget::itemDoubleClicked,
        this, &MainWindow::onConnectionItemDoubleClicked);

    mainLayout->addWidget(m_welcomeTitleLabel);
    mainLayout->addSpacing(10);
    mainLayout->addWidget(m_welcomeDescLabel);
    mainLayout->addSpacing(30);
    mainLayout->addWidget(m_welcomeHistoryLabel);
    mainLayout->addSpacing(10);
    mainLayout->addWidget(m_connectionList);
    mainLayout->addStretch();
    m_welcomeWidget->setLayout(mainLayout);
}

void MainWindow::createSystemTrayIcon() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setContextMenu(m_trayIconMenu);
    m_trayIcon->setIcon(QIcon(":/icons/app.svg"));
    m_trayIcon->setToolTip(tr("远程桌面"));
    m_trayIcon->show();
}
