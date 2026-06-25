#include "MainWindow.h"
#include "ConnectionDialog.h"
#include "SettingsDialog.h"
#include "../../server/ServerManager.h"
#include "../../client/session/RemoteDesktopSession.h"
#include "../../client/network/ConnectionManager.h"
#include "../../server/dataflow/QueueManager.h"
#include "../core/threading/ThreadManager.h"
#include "../../server/simulator/InputSimulator.h"

#include "../core/config/UiConstants.h"
#include "../core/config/MessageConstants.h"
#include "../core/logging/LoggingCategories.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach_init.h>
#include <mach/task.h>
#endif

#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QMenu>
#include <QtGui/QAction>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QLabel>
#include <QtCore/QTimer>
#include <QtCore/QFile>
#include <QtCore/QDateTime>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QUuid>
#include <QtGui/QIcon>
#include <QtGui/QCloseEvent>
#include <QtCore/QEvent>
#include <QtWidgets/QListWidgetItem>


MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_centralWidget(nullptr)
    , m_mainSplitter(nullptr)
    , m_connectionList(nullptr)
    , m_welcomeWidget(nullptr)
    , m_welcomeTitleLabel(nullptr)
    , m_welcomeDescLabel(nullptr)
    , m_welcomeHistoryLabel(nullptr)
    , m_trayIcon(nullptr)
    , m_connectionDialog(nullptr)
    , m_settingsDialog(nullptr)
    , m_serverManager(nullptr)
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

    // 创建核心基础设施（DI 注入链的起点）
    m_threadManager = new ThreadManager(this);
    m_queueManager = new QueueManager(this);

    // 创建管理器组件
    m_serverManager = new ServerManager(this, m_threadManager, m_queueManager);

    // 设置连接
    setupConnections();

    // 预创建设置对话框，避免首次点击时 UI 线程阻塞
    // （样式表解析 + 密码解密等操作集中在启动阶段完成）
    m_settingsDialog = new SettingsDialog(this);
    m_settingsDialog->hide();

    // 加载设置
    loadSettings();

    // 设置窗口属性
    setWindowTitle(tr("Qt远程桌面"));
    setMinimumSize(UIConstants::MIN_WINDOW_WIDTH, UIConstants::MIN_WINDOW_HEIGHT);
    resize(UIConstants::MAIN_WINDOW_WIDTH, UIConstants::MAIN_WINDOW_HEIGHT);
}

MainWindow::~MainWindow() {
    qCInfo(lcUI) << "MainWindow::~MainWindow() - Destructor started";

    // 在析构函数中进行最后的资源清理
    // 注意：此时不应该再调用可能触发信号的方法

    // 1. 断开所有信号连接，防止在析构过程中触发信号
    if ( m_serverManager ) {
        disconnect(m_serverManager, nullptr, this, nullptr);
    }

    // 断开并清理所有会话
    for (auto* session : m_sessions) {
        disconnect(session, nullptr, this, nullptr);
        session->close();
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

    qCInfo(lcUI) << "MainWindow::~MainWindow() - Destructor complete";
}

// createActions/createMenus/createToolBars/createStatusBar/createCentralWidget/
// createWelcomeWidget/createSystemTrayIcon 实现见 MainWindowLayout.cpp

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

    // 系统托盘连接
    if ( m_trayIcon ) {
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);
        connect(m_minimizeAction, &QAction::triggered, this, &QWidget::hide);
        connect(m_maximizeAction, &QAction::triggered, this, &QWidget::showMaximized);
        connect(m_restoreAction, &QAction::triggered, this, &QWidget::showNormal);
    }

    // 性能信息定时更新（每 2 秒）
    auto* perfTimer = new QTimer(this);
    connect(perfTimer, &QTimer::timeout, this, &MainWindow::updatePerformanceInfo);
    perfTimer->start(2000);
    updatePerformanceInfo();
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
    qCInfo(lcUI) << "MainWindow::saveSettings() - Settings saved";
}

void MainWindow::closeEvent(QCloseEvent* event) {
    qCInfo(lcUI) << "MainWindow::closeEvent() - Close event started";

    // 防止重复关闭
    if ( m_isShuttingDown ) {
        qCInfo(lcUI) << "MainWindow::closeEvent() - Already shutting down, ignoring duplicate close";
        event->accept();
        return;
    }

    m_isShuttingDown = true;

    // 保存设置
    saveSettings();

    // 在客户端模式下，直接退出应用程序
    if ( m_clientMode ) {
        qCInfo(lcUI) << "MainWindow::closeEvent() - Client mode, closing main window and exiting application";

        // 断开所有客户端连接（先断开 finished 信号，防止 close() 触发 removeOne 修改容器）
        for (auto* session : m_sessions) {
            disconnect(session, &RemoteDesktopSession::finished, this, nullptr);
            session->close();
        }
        qDeleteAll(m_sessions);
        m_sessions.clear();

        // 接受关闭事件
        event->accept();

        // 强制退出应用程序
        QApplication::exit(0);
        return;
    }

    // 服务器模式下执行优雅停止序列
    gracefulShutdown();

    qCInfo(lcUI) << "MainWindow::closeEvent() - Server stopped";

    // 接受关闭事件
    event->accept();

    qCInfo(lcUI) << "MainWindow::closeEvent() - Close event complete";
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if ( event->type() == QEvent::WindowStateChange ) {
        if ( isMinimized() && m_trayIcon && m_trayIcon->isVisible() ) {
            hide();
        }
    } else if ( event->type() == QEvent::LanguageChange ) {
        qCInfo(lcUI) << "MainWindow::changeEvent - LanguageChange received, calling retranslateUi";
        retranslateUi();
        qCInfo(lcUI) << "MainWindow::changeEvent - retranslateUi completed";
    }
}

void MainWindow::retranslateUi() {
    qCInfo(lcUI) << "MainWindow::retranslateUi - starting UI retranslation";
    setWindowTitle(tr("Qt远程桌面"));

    // 动作文本
    m_newConnectionAction->setText(tr("新建连接(&N)..."));
    m_newConnectionAction->setStatusTip(tr("创建新的远程连接"));
    m_exitAction->setText(tr("退出(&X)"));
    m_exitAction->setStatusTip(tr("退出应用程序"));
    m_connectAction->setText(tr("连接(&C)"));
    m_connectAction->setStatusTip(tr("连接到远程主机"));
    m_settingsAction->setText(tr("设置(&S)..."));
    m_settingsAction->setStatusTip(tr("配置应用程序设置"));
    m_aboutAction->setText(tr("关于(&A)"));
    m_aboutAction->setStatusTip(tr("显示应用程序的关于对话框"));
    m_aboutQtAction->setText(tr("关于Qt(&Q)"));
    m_aboutQtAction->setStatusTip(tr("显示Qt库的关于对话框"));

    // 系统托盘动作
    m_minimizeAction->setText(tr("最小化(&N)"));
    m_maximizeAction->setText(tr("最大化(&X)"));
    m_restoreAction->setText(tr("恢复(&R)"));

    // 菜单标题
    m_fileMenu->setTitle(tr("文件(&F)"));
    m_connectionMenu->setTitle(tr("连接(&C)"));
    m_toolsMenu->setTitle(tr("工具(&T)"));
    m_helpMenu->setTitle(tr("帮助(&H)"));

    // 工具栏
    m_mainToolBar->setWindowTitle(tr("主工具栏"));

    // 状态栏
    m_connectionStatusLabel->setText(tr("未连接"));
    m_serverStatusLabel->setText(tr("服务器已停止"));
    updatePerformanceInfo();
    statusBar()->showMessage(tr("就绪"));

    // 欢迎页面
    if ( m_welcomeTitleLabel ) m_welcomeTitleLabel->setText(tr("欢迎使用Qt远程桌面"));
    if ( m_welcomeDescLabel ) m_welcomeDescLabel->setText(tr("使用左侧按钮连接到远程计算机。"));
    if ( m_welcomeHistoryLabel ) m_welcomeHistoryLabel->setText(tr("连接历史记录"));

    // 刷新连接历史列表项中的翻译文本
    for (int i = 0; i < m_connectionList->count(); ++i) {
        QListWidgetItem* item = m_connectionList->item(i);
        if ( item ) {
            QString host = item->data(Qt::UserRole).toString();
            int port = item->data(Qt::UserRole + 1).toInt();
            QString connTime = item->data(Qt::UserRole + 2).toString();
            QLabel* label = qobject_cast<QLabel*>(m_connectionList->itemWidget(item));
            if ( label ) {
                label->setText(formatConnectionText(host, port, connTime));
            }
        }
    }

    qCInfo(lcUI) << "MainWindow::retranslateUi - UI retranslation done, windowTitle:" << windowTitle();
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
    if (!m_sessions.isEmpty()) {
        for (auto* session : m_sessions) {
            disconnect(session, &RemoteDesktopSession::finished, this, nullptr);
            session->close();
        }
        qDeleteAll(m_sessions);
        m_sessions.clear();
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
    if ( !checkMacOSAccessibilityPermission() ) {
        QMessageBox::warning(this, tr("需要辅助功能权限"),
            tr("<p>Qt远程桌面需要<b>辅助功能权限</b>才能模拟鼠标和键盘输入。</p>"
                "<p>请按照以下步骤授予权限：</p>"
                "<ol>"
                "<li>打开<b>系统偏好设置</b></li>"
                "<li>选择<b>安全性与隐私</b></li>"
                "<li>点击<b>隐私</b>标签</li>"
                "<li>在左侧列表中选择<b>辅助功能</b></li>"
                "<li>点击左下角的锁图标解锁</li>"
                "<li>在右侧列表中勾选<b>CrossRemoteDesktop</b></li>"
                "</ol>"
                "<p>授予权限后，请重启应用程序。</p>"));
        // 尝试打开系统设置
        requestMacOSAccessibilityPermission();
        return;
    }
#endif

    // 从 QSettings 读取监听端口，与 SettingsDialog 通信页保持一致
    QSettings settings;
    int port = settings.value("Server/listenPort", UIConstants::DEFAULT_SERVER_PORT).toInt();
    m_serverManager->startServer(port);
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
	m_settingsDialog->show();
	m_settingsDialog->raise();
	m_settingsDialog->activateWindow();
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
    // 断开所有客户端连接（先断开 finished 信号，防止 close() 触发 removeOne 修改容器）
    for (auto* session : m_sessions) {
        disconnect(session, &RemoteDesktopSession::finished, this, nullptr);
        session->close();
    }
    qDeleteAll(m_sessions);
    m_sessions.clear();

    // 停止服务器
    if ( m_serverManager && m_serverManager->isServerRunning() ) {
        m_serverManager->stopServer();
    }

    // 保存设置
    saveSettings();

    // 退出应用程序
    QApplication::quit();
}

// ===== 性能信息更新 =====

#ifdef Q_OS_WIN
namespace {
    // 上一次 CPU 时间（FILETIME → ULONGLONG 以 100ns 为单位）
    ULONGLONG g_prevKernelTime = 0;
    ULONGLONG g_prevUserTime = 0;
    ULONGLONG g_prevWallTime = 0;
}

static ULONGLONG fileTimeToU64(const FILETIME& ft) {
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
#endif

void MainWindow::updatePerformanceInfo()
{
#ifdef Q_OS_WIN
    FILETIME createTime, exitTime, kernelTime, userTime, currentTime;
    GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime);
    GetSystemTimeAsFileTime(&currentTime);

    const ULONGLONG nowKernel = fileTimeToU64(kernelTime);
    const ULONGLONG nowUser   = fileTimeToU64(userTime);
    const ULONGLONG nowWall   = fileTimeToU64(currentTime);

    // 采集第一次数据后等待下一次再计算
    if (g_prevWallTime == 0) {
        g_prevKernelTime = nowKernel;
        g_prevUserTime   = nowUser;
        g_prevWallTime   = nowWall;

        // 首次仍显示内存信息（已有数据）
        PROCESS_MEMORY_COUNTERS_EX pmc;
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            const double memMB = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
            m_performanceLabel->setText(tr("CPU: --% | 内存: %1 MB").arg(memMB, 0, 'f', 1));
        }
        return;
    }

    // 计算 CPU 使用率
    const ULONGLONG deltaTime    = nowKernel + nowUser - g_prevKernelTime - g_prevUserTime;
    const ULONGLONG deltaWall    = nowWall - g_prevWallTime;

    g_prevKernelTime = nowKernel;
    g_prevUserTime   = nowUser;
    g_prevWallTime   = nowWall;

    // 获取逻辑处理器数量
    static DWORD numProcessors = 0;
    if (numProcessors == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        numProcessors = si.dwNumberOfProcessors;
    }

    const double cpuPercent = (deltaWall > 0)
        ? (static_cast<double>(deltaTime) / static_cast<double>(deltaWall)) * 100.0
        : 0.0;

    // 获取内存使用（Working Set，单位 MB）
    PROCESS_MEMORY_COUNTERS_EX pmc;
    pmc.cb = sizeof(pmc);
    double memMB = 0.0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        memMB = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }

    m_performanceLabel->setText(tr("CPU: %1% | 内存: %2 MB")
        .arg(cpuPercent, 0, 'f', 1)
        .arg(memMB, 0, 'f', 1));

#elif defined(Q_OS_LINUX)
    // Linux: 读取 /proc/self/stat
    QFile statFile("/proc/self/stat");
    if (statFile.open(QIODevice::ReadOnly)) {
        const QByteArray data = statFile.readAll();
        const QList<QByteArray> fields = data.split(' ');
        // 字段 13=utime, 14=stime (0-based: fields[13], fields[14])
        if (fields.size() > 14) {
            const long ticks = fields[13].toLong() + fields[14].toLong();
            static long prevTicks = 0;
            static qint64 prevMs = 0;
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (prevMs > 0) {
                const double cpuPercent = (static_cast<double>(ticks - prevTicks) / sysconf(_SC_CLK_TCK))
                    / (static_cast<double>(nowMs - prevMs) / 1000.0) * 100.0;
                m_performanceLabel->setText(tr("CPU: %1%").arg(cpuPercent, 0, 'f', 1));
            }
            prevTicks = ticks;
            prevMs = nowMs;
        }
    }

#elif defined(Q_OS_MACOS)
    // macOS: mach task_info
    // 注：macOS 上精确 CPU 需要 proc_pid_rusage，此处简化
    struct task_basic_info tbi;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&tbi), &count) == KERN_SUCCESS) {
        const double memMB = static_cast<double>(tbi.resident_size) / (1024.0 * 1024.0);
        m_performanceLabel->setText(tr("内存: %1 MB").arg(memMB, 0, 'f', 1));
    }
#endif
}

void MainWindow::gracefulShutdown() {
    qCInfo(lcUI) << "MainWindow::gracefulShutdown() - Starting graceful shutdown";

    // 断开所有客户端连接
    if ( !m_sessions.isEmpty() ) {
        qCInfo(lcUI) << "MainWindow::gracefulShutdown() - Disconnecting all clients";
        for (auto* session : m_sessions) {
            disconnect(session, &RemoteDesktopSession::finished, this, nullptr);
            session->close();
        }
        qDeleteAll(m_sessions);
        m_sessions.clear();
    }

    // 停止服务器（无论当前标记是否显示正在运行，均调用优雅关闭以保证最终态日志输出与资源释放的幂等性）
    if ( m_serverManager ) {
        qCInfo(lcUI) << "MainWindow::gracefulShutdown() - Stopping server";

        // 使用gracefulShutdown方法进行同步停止（内部具备幂等保护与最终态日志输出）
        m_serverManager->gracefulShutdown();

        qCInfo(lcUI) << "MainWindow::gracefulShutdown() - Server stopped";
    }

    // 断开所有信号连接，防止在退出过程中触发回调
    if ( m_serverManager ) {
        disconnect(m_serverManager, nullptr, this, nullptr);
    }
    // 注：m_sessions 已在上面通过 qDeleteAll + clear() 清理完毕，无需再次遍历

    // 停止并等待所有工作线程退出——std::_Exit 不触发析构，必须在此显式回收
    if ( m_threadManager ) {
        m_threadManager->destroyAllThreads();
        qCInfo(lcUI) << "MainWindow::gracefulShutdown() - All threads destroyed";
    }

    // 隐藏系统托盘图标——std::_Exit 跳过 ~MainWindow() 析构，
    // 必须在此显式调用 hide() 以发送 NIM_DELETE 通知 Windows 移除图标，
    // 否则每次退出都会残留一个孤儿托盘图标，多次启动后累积成多个。
    if ( m_trayIcon ) {
        m_trayIcon->hide();
        qCInfo(lcUI) << "MainWindow::gracefulShutdown() - Tray icon hidden";
    }

    qCInfo(lcUI) << "MainWindow::gracefulShutdown() - Graceful shutdown complete";

    // 正常退出应用程序
    QCoreApplication::quit();
}

void MainWindow::showConnectionDialog() {
    if ( !m_connectionDialog ) {
        m_connectionDialog = new ConnectionDialog(this);
    }

    // 预填默认端口（优先服务端运行端口，否则从 QSettings 读取）
    int defaultPort = UIConstants::DEFAULT_SERVER_PORT;
    if (m_serverManager && m_serverManager->isServerRunning()) {
        defaultPort = m_serverManager->getCurrentPort();
    } else {
        QSettings settings;
        defaultPort = settings.value("Server/listenPort", UIConstants::DEFAULT_SERVER_PORT).toInt();
    }
    m_connectionDialog->setDefaultPort(defaultPort);

    // 预填用户名
    {
        QSettings settings;
        const QString username = settings.value("Server/username").toString();
        if (!username.isEmpty()) {
            m_connectionDialog->setUsername(username);
        }
    }

    if ( m_connectionDialog->exec() == QDialog::Accepted ) {
        // 处理连接请求
        QString host = m_connectionDialog->getHostAddress();
        int port = m_connectionDialog->getPort();

        // 直接连接到主机
        connectToHostDirectly(host, port);
    }
}

void MainWindow::connectToHostDirectly(const QString& host, int port) {
    QString connectionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ConnectionParams params;
    params.host = host;
    params.port = port;
    auto* session = new RemoteDesktopSession(params, connectionId, this);

    connect(session, &RemoteDesktopSession::finished, this, [this, session](const QString& id) {
        Q_UNUSED(id);
        m_sessions.removeOne(session);
        session->deleteLater();
        if (m_sessions.isEmpty()) {
            onAllConnectionsClosed();
        }
    });

    connect(session, &RemoteDesktopSession::errorOccurred, this, [this](const RdError& err) {
        qCWarning(lcSession) << "RemoteDesktopSession error:" << err.logLabel();
        updateConnectionStatus(err.logLabel());
    });

    m_sessions.append(session);

    // 添加到连接历史
    addConnectionToHistory(host, port);

    session->start();
}

void MainWindow::onConnectionEstablished(const QString& connectionId) {
    qCInfo(lcApp) << "MainWindow::onConnectionEstablished - Connection established for:" << connectionId;

    // 从会话列表中查找对应会话以获取主机/端口信息
    for (auto* session : m_sessions) {
        if (session->connectionId() == connectionId) {
            auto* cm = session->connectionManager();
            if (cm) {
                QString host = cm->currentHost();
                int port = cm->currentPort();
                if (!host.isEmpty() && port > 0) {
                    addConnectionToHistory(host, port);
                }
            }
            break;
        }
    }
}

void MainWindow::onServerStarted(quint16 port) {
    qCInfo(lcApp) << "MainWindow::onServerStarted() called with port:" << port;
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
    qCInfo(lcApp) << "MainWindow::onServerStopped() called";
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

void MainWindow::onServerError(const RdError& error) {
    qCWarning(lcApp) << "MainWindow::onServerError() called with error:" << error.logLabel();
    updateServerStatus(tr("服务器启动失败"));
    QMessageBox::warning(this, tr("服务器错误"), error.logLabel());
}

void MainWindow::onClientConnected(const QString& clientId) {
    qCInfo(lcApp) << "MainWindow::onClientConnected() called with clientId:" << clientId;
    updateConnectionStatus(tr("客户端已连接: %1").arg(clientId));
}

void MainWindow::onClientDisconnected(const QString& clientId) {
    qCInfo(lcApp) << "MainWindow::onClientDisconnected() called with clientId:" << clientId;
    updateConnectionStatus(tr("客户端已断开: %1").arg(clientId));
}

void MainWindow::onClientAuthenticated(const QString& clientId) {
    qCInfo(lcApp) << "MainWindow::onClientAuthenticated() called with clientId:" << clientId;
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
    qCDebug(lcApp) << "MainWindow::cleanupConnection for:" << connectionId;
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
            // 添加新项目(会自动插入到顶部)
            QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            createConnectionListItem(host, port, currentTime);

            // 保存到历史记录
            saveConnectionHistory();
        } else {
            // 如果连接已存在,更新连接时间并移到顶部
            QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

            // 先从列表中移除该项
            int row = m_connectionList->row(existingItem);
            QListWidgetItem* item = m_connectionList->takeItem(row);

            // 更新项目数据
            item->setData(Qt::UserRole, host);
            item->setData(Qt::UserRole + 1, port);
            item->setData(Qt::UserRole + 2, currentTime);

            // 插入到列表顶部
            m_connectionList->insertItem(0, item);

            // 更新QLabel内容
            QLabel* label = qobject_cast<QLabel*>(m_connectionList->itemWidget(item));
            if ( label ) {
                label->setText(formatConnectionText(host, port, currentTime));
            } else {
                // 如果widget丢失,重新创建
                QLabel* newLabel = new QLabel(formatConnectionText(host, port, currentTime));
                newLabel->setWordWrap(true);
                newLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
                newLabel->setStyleSheet(
                    "QLabel {"
                    "    color: #2c3e50;"
                    "    padding: 15px 12px;"
                    "    background-color: transparent;"
                    "    font-size: 13px;"
                    "}"
                );
                m_connectionList->setItemWidget(item, newLabel);
            }

            // 保存到历史记录
            saveConnectionHistory();
        }

        m_connectionList->setCurrentRow(0);
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

    if ( m_connectionList->count() > 0 ) {
        m_connectionList->setCurrentRow(0);
    }
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

    // 顺序加载历史记录:
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

        qCInfo(lcUI) << "Application set to client mode";
    } else {
        // 服务器模式：正常启动服务器
        setWindowTitle(tr("Qt远程桌面"));
        qCInfo(lcUI) << "Application set to server mode";

        // 延迟启动服务器
        QTimer::singleShot(500, this, &MainWindow::startServer);
    }
}

QString MainWindow::formatConnectionText(const QString& host, int port, const QString& connectionTime) {
    return tr("主机: %1\n端口: %2\n连接时间: %3")
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

    item->setSizeHint(QSize(0, 120));

    // 添加到列表末尾,保持配置文件中的顺序
    m_connectionList->addItem(item);
    m_connectionList->setItemWidget(item, label);

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
    qCDebug(lcMainWindow) << "MainWindow::onAllConnectionsClosed() - All client connections closed";

    // 只有在客户端模式下才退出应用程序
    // 服务器模式下应该保持运行，等待新的客户端连接
    if ( m_clientMode ) {
        qCDebug(lcMainWindow) << "MainWindow::onAllConnectionsClosed() - Client mode, all connections closed, exiting application";
        QApplication::quit();
    } else {
        qCDebug(lcMainWindow) << "服务器模式下所有连接已关闭，保持运行状态";
    }
}

#ifdef Q_OS_MACOS

bool MainWindow::checkMacOSAccessibilityPermission() {
    // 检查辅助功能权限
    return AXIsProcessTrusted();
}

bool MainWindow::requestMacOSAccessibilityPermission() {
    // 创建带提示的选项字典，会弹出系统对话框引导用户授权
    const void* keys[] = { kAXTrustedCheckOptionPrompt };
    const void* values[] = { kCFBooleanTrue };

    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    Boolean trusted = AXIsProcessTrustedWithOptions(options);

    if ( options ) {
        CFRelease(options);
    }

    return trusted;
}
#endif