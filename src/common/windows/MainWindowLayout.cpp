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
// 创建 Actions（托盘菜单所需的全局 Action）
// ============================================================

void MainWindow::createActions()
{
    // 使用 IconThemeProvider 设置所有工具栏/菜单图标（替代 .ui 中的硬编码路径）
    // 全局 Actions

    m_exitAction = new QAction(tr("退出"), this);
    m_exitAction->setToolTip(tr("退出"));
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
    statusBar()->setSizeGripEnabled(false);
}

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
    m_trayIcon->setIcon(IconThemeProvider::icon("app"));
    m_trayIcon->setToolTip(tr("远程桌面"));
    m_trayIcon->show();
}
