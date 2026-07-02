// MainWindowLayout.cpp — UI 构建方法实现
// 从 MainWindow.cpp 分离，保持文件聚焦于业务逻辑

#include "MainWindow.h"
#include "common/core/theme/IconThemeProvider.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QToolButton>

// ============================================================
// 创建 Actions（仅保留全局快捷键绑定的 Action，菜单/工具栏入口由 HamburgerMenu 替代）
// ============================================================

void MainWindow::createActions()
{
    // 使用 IconThemeProvider 设置所有工具栏/菜单图标（替代 .ui 中的硬编码路径）
    // 全局快捷键 Actions
    m_newConnectionAction = new QAction(this);
    m_newConnectionAction->setShortcut(QKeySequence::New);
    m_newConnectionAction->setToolTip(tr("新建连接 (Ctrl+N)"));
    m_newConnectionAction->setIcon(IconThemeProvider::icon("new_connection"));
    addAction(m_newConnectionAction);

    m_connectAction = new QAction(this);
    m_connectAction->setShortcut(QKeySequence(tr("Ctrl+O")));
    m_connectAction->setToolTip(tr("连接 (Ctrl+O)"));
    m_connectAction->setIcon(IconThemeProvider::icon("connect"));
    addAction(m_connectAction);

    m_settingsAction = new QAction(this);
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    m_settingsAction->setToolTip(tr("设置 (Ctrl+,)"));
    m_settingsAction->setIcon(IconThemeProvider::icon("settings"));
    addAction(m_settingsAction);

    m_exitAction = new QAction(tr("退出"), this);
    m_exitAction->setShortcut(QKeySequence::Quit);
    m_exitAction->setToolTip(tr("退出 (Ctrl+Q)"));
    m_exitAction->setIcon(IconThemeProvider::icon("exit"));
    addAction(m_exitAction);

    // 托盘 Actions
    m_restoreAction  = new QAction(tr("恢复(&R)"), this);
}

// ============================================================
// 状态栏（保留当前逻辑，移除硬编码 QSS——由主题 QSS 统一控制）
// ============================================================

void MainWindow::createStatusBar()
{
    m_connectionStatusLabel = new QLabel(tr("连接：未连接"));
    m_connectionStatusLabel->setMinimumWidth(120);

    m_serverStatusLabel = new QLabel(tr("服务器：已停止"));
    m_serverStatusLabel->setMinimumWidth(120);

    m_performanceLabel = new QLabel(tr("CPU: 0% | 内存: 0MB"));
    m_performanceLabel->setMinimumWidth(150);

    statusBar()->addPermanentWidget(m_connectionStatusLabel);
    statusBar()->addPermanentWidget(m_serverStatusLabel);
    statusBar()->addPermanentWidget(m_performanceLabel);

    statusBar()->showMessage(tr("就绪"));
}

// ============================================================
// 中央区域 + 欢迎页布局
// 注：已迁移到 mainwindow.ui，通过 ui->setupUi(this) 加载。
//     自定义控件 HamburgerMenu / ConnectionPanel 通过 promotion 集成。
// ============================================================

// ============================================================
// 系统托盘（不变）
// ============================================================

void MainWindow::createSystemTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIconMenu = new QMenu(this);

    m_trayIconMenu->addAction(m_restoreAction);
    m_trayIconMenu->addSeparator();
    m_trayIconMenu->addAction(m_exitAction);

    m_trayIcon->setContextMenu(m_trayIconMenu);
    m_trayIcon->setIcon(QIcon(":/icons/app.svg"));
    m_trayIcon->setToolTip(tr("远程桌面"));
    m_trayIcon->show();
}
