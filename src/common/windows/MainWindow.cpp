#include "MainWindow.h"
#include "ConnectionDialog.h"
#include "ConnectionPanel.h"
#include "SettingsDialog.h"
#include "HamburgerMenu.h"
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
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QMenu>
#include <QtGui/QAction>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
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


MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_centralWidget(nullptr)
    , m_welcomeTitleLabel(nullptr)
    , m_hamburgerMenu(nullptr)
    , m_connectionPanel(nullptr)
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
    setFixedSize(960, 680);
    setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint
                   | Qt::WindowCloseButtonHint
                   | Qt::MSWindowsFixedSizeDialogHint);

    // --- 主题初始化 ---
    m_themeMode = m_settings->value("UI/theme", "dark").toString();
    applyTheme();

    // --- 汉堡菜单信号连接 ---
    connect(m_hamburgerMenu, &HamburgerMenu::newConnection,
            this, &MainWindow::newConnection);

    connect(m_hamburgerMenu, &HamburgerMenu::openSettings,
            this, &MainWindow::showSettings);
    connect(m_hamburgerMenu, &HamburgerMenu::showAbout,
            this, &MainWindow::showAbout);
    connect(m_hamburgerMenu, &HamburgerMenu::themeToggled,
            this, &MainWindow::toggleTheme);
}

MainWindow::~MainWindow() {
    qCDebug(lcUIMainWindow) << "MainWindow::~MainWindow() - Destructor started";

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

    qCDebug(lcUIMainWindow) << "MainWindow::~MainWindow() - Destructor complete";
}

// createActions/createMenus/createToolBars/createStatusBar/createCentralWidget/
// createWelcomeWidget/createSystemTrayIcon 实现见 MainWindowLayout.cpp

void MainWindow::setupConnections() {
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

    // 快捷键全局连接（无系统托盘时仍有效）
    connect(m_newConnectionAction, &QAction::triggered, this, &MainWindow::newConnection);
    connect(m_connectAction, &QAction::triggered, this, &MainWindow::newConnection);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    connect(m_exitAction, &QAction::triggered, this, &MainWindow::exitApplication);

    // 系统托盘连接
    if ( m_trayIcon ) {
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);
        connect(m_restoreAction, &QAction::triggered, this, &QWidget::showNormal);
    }

    // ConnectionPanel 信号连接
    if (m_connectionPanel) {
        connect(m_connectionPanel, &ConnectionPanel::contentChanged,
                this, [this]() {
            if (m_connectionPanel) {
                m_connectionPanel->saveHistory(*m_settings);
            }
        });

        connect(m_connectionPanel, &ConnectionPanel::connectRequested,
                this, [this](const QString &host, int port, const QString &hostname) {
            ConnectionParams params;
            params.host = host;
            params.port = port;
            params.hostname = hostname;
            // 从已有历史记录回填分辨率
            if (m_connectionPanel) {
                HistoryEntry entry = m_connectionPanel->entryFor(host, port);
                params.windowWidth = entry.resWidth;
                params.windowHeight = entry.resHeight;
            }
            connectToHostDirectly(params);
        });

        connect(m_connectionPanel, &ConnectionPanel::editRequested,
                this, [this](const QString &host, int port) {
            if (!m_connectionPanel)
                return;
            HistoryEntry entry = m_connectionPanel->entryFor(host, port);
            if (!m_connectionDialog) {
                m_connectionDialog = new ConnectionDialog(this);
            }
            m_connectionDialog->setHostname(entry.hostname);
            m_connectionDialog->setHostAddress(entry.host);
            m_connectionDialog->setPort(entry.port);

            if (m_connectionDialog->exec() == QDialog::Accepted) {
                // 移除旧条目
                m_connectionPanel->removeEntry(host, port);
                // 添加更新后的条目
                ConnectionParams params;
                params.hostname = m_connectionDialog->getHostname();
                params.host     = m_connectionDialog->getHostAddress();
                params.port     = m_connectionDialog->getPort();
                m_connectionPanel->addEntry(
                    params.host.isEmpty() ? params.hostname : params.host,
                    params.port, params.hostname,
                    entry.resWidth, entry.resHeight);
                m_connectionPanel->saveHistory(*m_settings);
            }
        });
    }

    // 性能信息定时更新（每 2 秒）
    auto* perfTimer = new QTimer(this);
    connect(perfTimer, &QTimer::timeout, this, &MainWindow::updatePerformanceInfo);
    perfTimer->start(2000);
    updatePerformanceInfo();
}

void MainWindow::loadSettings() {
    // 加载历史连接记录
    if (m_connectionPanel) {
        m_connectionPanel->loadHistory(*m_settings);
    }

    // 检查是否需要自动启动服务器
    bool autoStartServer = m_settings->value("Server/autoStart", false).toBool();
    if ( autoStartServer ) {
        // 延迟启动服务器，确保UI完全初始化
        QTimer::singleShot(100, this, &MainWindow::startServer);
    }
}

void MainWindow::saveSettings() {
    // 保存历史连接记录
    if (m_connectionPanel) {
        m_connectionPanel->saveHistory(*m_settings);
    }

    // 统一输出保存设置日志，便于测试用例判断
    qCDebug(lcUIMainWindow) << "MainWindow::saveSettings() - Settings saved";
}

void MainWindow::closeEvent(QCloseEvent* event) {
    qCInfo(lcUIMainWindow) << "MainWindow::closeEvent() - Close event started";

    // 防止重复关闭
    if ( m_isShuttingDown ) {
        qCDebug(lcUIMainWindow) << "MainWindow::closeEvent() - Already shutting down, ignoring duplicate close";
        event->accept();
        return;
    }

    m_isShuttingDown = true;

    // 保存设置
    saveSettings();

    // 在客户端模式下，直接退出应用程序
    if ( m_clientMode ) {
        qCInfo(lcUIMainWindow) << "MainWindow::closeEvent() - Client mode, closing main window and exiting application";

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

    qCInfo(lcUIMainWindow) << "MainWindow::closeEvent() - Server stopped";

    // 接受关闭事件
    event->accept();

    qCInfo(lcUIMainWindow) << "MainWindow::closeEvent() - Close event complete";
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if ( event->type() == QEvent::WindowStateChange ) {
        if ( isMinimized() && m_trayIcon && m_trayIcon->isVisible() ) {
            hide();
        }
    } else if ( event->type() == QEvent::LanguageChange ) {
        qCInfo(lcUIMainWindow) << "MainWindow::changeEvent - LanguageChange received, calling retranslateUi";
        retranslateUi();
        qCInfo(lcUIMainWindow) << "MainWindow::changeEvent - retranslateUi completed";
    }
}

void MainWindow::retranslateUi() {
    qCInfo(lcUIMainWindow) << "MainWindow::retranslateUi - starting UI retranslation";
    setWindowTitle(m_clientMode ? tr("Qt远程桌面 - 客户端模式") : tr("Qt远程桌面"));

    // 全局快捷键动作
    m_exitAction->setText(tr("退出"));

    // 系统托盘动作
    m_restoreAction->setText(tr("恢复(&R)"));

    // 状态栏
    m_connectionStatusLabel->setText(tr("连接：未连接"));
    m_serverStatusLabel->setText(tr("服务器：已停止"));
    updatePerformanceInfo();
    statusBar()->showMessage(tr("就绪"));

    // 连接历史面板
    if ( m_connectionPanel ) {
        m_connectionPanel->retranslateUi();
    }

    // 汉堡菜单
    if ( m_hamburgerMenu ) {
        m_hamburgerMenu->retranslateUi();
    }

    qCInfo(lcUIMainWindow) << "MainWindow::retranslateUi - UI retranslation done, windowTitle:" << windowTitle();
}

// 槽函数实现
void MainWindow::newConnection() {
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
    qCInfo(lcUIMainWindow) << "MainWindow::gracefulShutdown() - Starting graceful shutdown";

    // 断开所有客户端连接
    if ( !m_sessions.isEmpty() ) {
        qCInfo(lcUIMainWindow) << "MainWindow::gracefulShutdown() - Disconnecting all clients";
        for (auto* session : m_sessions) {
            disconnect(session, &RemoteDesktopSession::finished, this, nullptr);
            session->close();
        }
        qDeleteAll(m_sessions);
        m_sessions.clear();
    }

    // 停止服务器（无论当前标记是否显示正在运行，均调用优雅关闭以保证最终态日志输出与资源释放的幂等性）
    if ( m_serverManager ) {
        qCInfo(lcUIMainWindow) << "MainWindow::gracefulShutdown() - Stopping server";

        // 使用gracefulShutdown方法进行同步停止（内部具备幂等保护与最终态日志输出）
        m_serverManager->gracefulShutdown();

        qCInfo(lcUIMainWindow) << "MainWindow::gracefulShutdown() - Server stopped";
    }

    // 断开所有信号连接，防止在退出过程中触发回调
    if ( m_serverManager ) {
        disconnect(m_serverManager, nullptr, this, nullptr);
    }
    // 注：m_sessions 已在上面通过 qDeleteAll + clear() 清理完毕，无需再次遍历

    // 停止并等待所有工作线程退出——std::_Exit 不触发析构，必须在此显式回收
    if ( m_threadManager ) {
        m_threadManager->destroyAllThreads();
        qCInfo(lcUIMainWindow) << "MainWindow::gracefulShutdown() - All threads destroyed";
    }

    // 隐藏系统托盘图标——std::_Exit 跳过 ~MainWindow() 析构，
    // 必须在此显式调用 hide() 以发送 NIM_DELETE 通知 Windows 移除图标，
    // 否则每次退出都会残留一个孤儿托盘图标，多次启动后累积成多个。
    if ( m_trayIcon ) {
        m_trayIcon->hide();
        qCInfo(lcUIMainWindow) << "MainWindow::gracefulShutdown() - Tray icon hidden";
    }

    qCInfo(lcUIMainWindow) << "MainWindow::gracefulShutdown() - Graceful shutdown complete";

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
        // 从对话框提取全部参数
        ConnectionParams params;
        params.host     = m_connectionDialog->getHostAddress();
        params.port     = m_connectionDialog->getPort();
        params.hostname = m_connectionDialog->getHostname();
        params.username = m_connectionDialog->getUsername();
        params.password = m_connectionDialog->getPassword();

        params.fullScreen     = m_connectionDialog->getFullScreen();
        params.windowWidth    = m_connectionDialog->getWindowWidth();
        params.windowHeight   = m_connectionDialog->getWindowHeight();
        params.colorDepth     = m_connectionDialog->getColorDepth();
        params.imageQuality   = m_connectionDialog->getImageQuality();
        params.viewOnly       = m_connectionDialog->getViewOnly();
        params.shareClipboard = m_connectionDialog->getShareClipboard();
        params.showCursor     = m_connectionDialog->getShowCursor();

        // 对话框单位为秒，ConnectionParams 单位为毫秒
        params.connectionTimeout  = m_connectionDialog->getConnectionTimeout() * 1000;
        params.autoReconnect      = m_connectionDialog->getAutoReconnect();
        params.reconnectInterval  = m_connectionDialog->getReconnectInterval() * 1000;

        connectToHostDirectly(params);
    }
}

void MainWindow::connectToHostDirectly(const ConnectionParams& params) {
    QString connectionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
        qCWarning(lcClientSession) << "RemoteDesktopSession error:" << err.logLabel();
        updateConnectionStatus(err.logLabel());
    });

    m_sessions.append(session);

    if (m_connectionPanel) {
        m_connectionPanel->addEntry(params.host, params.port, params.hostname,
                                     params.windowWidth, params.windowHeight);
        m_connectionPanel->saveHistory(*m_settings);
    }

    // 启动会话（内部调用 connectToHost）
    session->start();
}

void MainWindow::onConnectionEstablished(const QString& connectionId) {
    qCInfo(lcApp) << "MainWindow::onConnectionEstablished - Connection established for:" << connectionId;
    // 注：历史记录已在 connectToHostDirectly() 中完整保存（含 hostname/分辨率），
    // 此处不再重复调用 addConnectionToHistory，避免以空参数覆盖已有完整数据。
}

void MainWindow::onServerStarted(quint16 port) {
    qCInfo(lcApp) << "MainWindow::onServerStarted() called with port:" << port;
    updateServerStatus(tr("服务器启动成功，端口: %1").arg(port));

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



void MainWindow::updateServerStatus(const QString& message) {
    // 检查ServerManager的连接状态
    m_serverStatusLabel->setText(message);
}

void MainWindow::updateConnectionStatus(const QString& message) {
    // 检查Client connect to server的连接状态
    m_connectionStatusLabel->setText(message);
}

void MainWindow::applyTheme()
{
    QString qssFile = (m_themeMode == "dark")
        ? ":/styles/dark.qss"
        : ":/styles/light.qss";

    QFile file(qssFile);
    if (file.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(file.readAll());
        qApp->setStyleSheet(styleSheet);
        file.close();
    }

    m_settings->setValue("UI/theme", m_themeMode);
}

void MainWindow::toggleTheme()
{
    m_themeMode = (m_themeMode == "dark") ? "light" : "dark";
    applyTheme();
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

        qCInfo(lcUIMainWindow) << "Application set to client mode";
    } else {
        // 服务器模式：正常启动服务器
        setWindowTitle(tr("Qt远程桌面"));
        qCInfo(lcUIMainWindow) << "Application set to server mode";

        // 延迟启动服务器
        QTimer::singleShot(500, this, &MainWindow::startServer);
    }
}

void MainWindow::onAllConnectionsClosed() {
    qCDebug(lcUIMainWindow) << "MainWindow::onAllConnectionsClosed() - All client connections closed";

    // 只有在客户端模式下才退出应用程序
    // 服务器模式下应该保持运行，等待新的客户端连接
    if ( m_clientMode ) {
        qCDebug(lcUIMainWindow) << "MainWindow::onAllConnectionsClosed() - Client mode, all connections closed, exiting application";
        QApplication::quit();
    } else {
        qCDebug(lcUIMainWindow) << "服务器模式下所有连接已关闭，保持运行状态";
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