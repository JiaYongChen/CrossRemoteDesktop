// MainWindowLayout.cpp — UI 构建方法实现
// 从 MainWindow.cpp 分离，保持文件聚焦于业务逻辑

#include "MainWindow.h"
#include "HamburgerMenu.h"
#include "ConnectionCard.h"

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QScrollArea>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QVBoxLayout>

// ============================================================
// 创建 Actions（仅保留全局快捷键绑定的 Action，菜单/工具栏入口由 HamburgerMenu 替代）
// ============================================================

void MainWindow::createActions()
{
    // 全局快捷键 Actions
    m_newConnectionAction = new QAction(this);
    m_newConnectionAction->setShortcut(QKeySequence::New);
    m_newConnectionAction->setToolTip(tr("新建连接 (Ctrl+N)"));
    addAction(m_newConnectionAction);

    m_connectAction = new QAction(this);
    m_connectAction->setShortcut(QKeySequence(tr("Ctrl+O")));
    m_connectAction->setToolTip(tr("连接 (Ctrl+O)"));
    addAction(m_connectAction);

    m_settingsAction = new QAction(this);
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    m_settingsAction->setToolTip(tr("设置 (Ctrl+,)"));
    addAction(m_settingsAction);

    m_exitAction = new QAction(tr("退出"), this);
    m_exitAction->setShortcut(QKeySequence::Quit);
    m_exitAction->setToolTip(tr("退出 (Ctrl+Q)"));
    addAction(m_exitAction);

    // 托盘 Actions
    m_minimizeAction = new QAction(tr("最小化(&N)"), this);
    m_maximizeAction = new QAction(tr("最大化(&X)"), this);
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

    statusBar()->addWidget(m_connectionStatusLabel);
    statusBar()->addPermanentWidget(m_serverStatusLabel);
    statusBar()->addPermanentWidget(m_performanceLabel);

    statusBar()->showMessage(tr("就绪"));
}

// ============================================================
// 中央区域：左侧导航栏 + 右侧欢迎页
// ============================================================

void MainWindow::createCentralWidget()
{
    m_centralWidget = new QWidget();
    setCentralWidget(m_centralWidget);

    auto *mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 左侧导航栏（HamburgerMenu）
    m_hamburgerMenu = new HamburgerMenu();
    mainLayout->addWidget(m_hamburgerMenu);

    // 右侧欢迎页
    createWelcomePage();
    m_welcomeWidget->setObjectName("welcomePage");
    mainLayout->addWidget(m_welcomeWidget, 1);
}

// ============================================================
// 欢迎页：Logo + 搜索框 + 卡片列表
// ============================================================

void MainWindow::createWelcomePage()
{
    m_welcomeWidget = new QWidget();
    auto *layout = new QVBoxLayout(m_welcomeWidget);
    layout->setAlignment(Qt::AlignHCenter);
    layout->setContentsMargins(0, 0, 0, 0);

    // --- Logo + 标题 ---
    auto *logoLabel = new QLabel();
    logoLabel->setPixmap(QPixmap(":/icons/app.svg").scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setAlignment(Qt::AlignCenter);
    layout->addSpacing(40);
    layout->addWidget(logoLabel, 0, Qt::AlignHCenter);

    m_welcomeTitleLabel = new QLabel(QStringLiteral("CrossRemoteDesktop"));
    m_welcomeTitleLabel->setObjectName("welcomeTitle");
    m_welcomeTitleLabel->setAlignment(Qt::AlignCenter);
    layout->addSpacing(12);
    layout->addWidget(m_welcomeTitleLabel, 0, Qt::AlignHCenter);

    // --- 搜索框 ---
    layout->addSpacing(24);
    m_searchBox = new QLineEdit();
    m_searchBox->setObjectName("searchBox");
    m_searchBox->setPlaceholderText(tr("搜索历史连接..."));
    m_searchBox->setFixedWidth(400);
    m_searchBox->setClearButtonEnabled(true);
    layout->addWidget(m_searchBox, 0, Qt::AlignHCenter);

    // --- 空状态提示 ---
    layout->addSpacing(16);
    m_emptyStateLabel = new QLabel(tr("暂无连接历史"));
    m_emptyStateLabel->setObjectName("emptyStateLabel");
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_emptyStateLabel, 0, Qt::AlignHCenter);

    // --- 卡片滚动区域 ---
    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // 隐藏滚动条

    m_cardContainer = new QWidget();
    m_cardLayout = new QVBoxLayout(m_cardContainer);
    m_cardLayout->setAlignment(Qt::AlignHCenter);
    m_cardLayout->setSpacing(12);
    m_cardLayout->setContentsMargins(0, 0, 0, 0);

    scrollArea->setWidget(m_cardContainer);
    layout->addWidget(scrollArea, 1);

    // 搜索框实时过滤
    connect(m_searchBox, &QLineEdit::textChanged, this, [this](const QString &text) {
        int visibleCount = 0;
        for (auto *card : m_connectionCards) {
            bool match = text.isEmpty()
                || card->property("searchKey").toString().contains(text, Qt::CaseInsensitive)
                || card->property("hostname").toString().contains(text, Qt::CaseInsensitive);
            card->setVisible(match);
            if (match) visibleCount++;
        }
        m_emptyStateLabel->setVisible(visibleCount == 0);
        m_emptyStateLabel->setText(
            text.isEmpty()
                ? tr("暂无连接历史")
                : tr("无匹配的连接记录"));
    });
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

    m_trayIconMenu->addAction(m_minimizeAction);
    m_trayIconMenu->addAction(m_maximizeAction);
    m_trayIconMenu->addAction(m_restoreAction);
    m_trayIconMenu->addSeparator();
    m_trayIconMenu->addAction(m_exitAction);

    m_trayIcon->setContextMenu(m_trayIconMenu);
    m_trayIcon->setIcon(QIcon(":/icons/app.svg"));
    m_trayIcon->setToolTip(tr("远程桌面"));
    m_trayIcon->show();
}
