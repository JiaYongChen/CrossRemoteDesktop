#include "MainWindow.h"
#include "ui_MainWindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach_init.h>
#include <mach/task.h>
#endif

#include <QtCore/QDateTime>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>

#include "client/session/RemoteDesktopSession.h"
#include "common/config/NetworkConstants.h"
#include "common/config/ProcessingConstants.h"
#include "common/config/SettingsManager.h"
#include "common/logging/LoggingCategories.h"
#include "common/theme/IconThemeProvider.h"
#include "common/theme/TitleBarTheme.h"
#include "common/threading/ThreadManager.h"
#include "ConnectionDialog.h"
#include "ConnectionPanel.h"
#include "NavPanel.h"
#include "server/dataflow/QueueManager.h"
#include "server/service/ServerService.h"
#include "SettingsDialog.h"
#include "common/platform/AutoStartManager.h"


MainWindow::MainWindow(SettingsManager *settings, QWidget *parent)
    : QMainWindow(parent)
    , m_navPanel(nullptr)
    , m_connectionPanel(nullptr)
    , m_trayIcon(nullptr)
    , m_connectionDialog(nullptr)
    , m_settingsDialog(nullptr)
    , m_serverService(nullptr)
    , m_settings(settings)
    , m_isShuttingDown(false) {
    // 初始化设置（由 main.cpp 注入）

    // 主题初始化必须在所有 UI 组件创建之前完成
    // （createActions 中会通过 IconThemeProvider::icon() 加载主题图标）
    m_themeMode = m_settings->getString("UI/theme", "dark");
    IconThemeProvider::setDarkMode(m_themeMode == "dark");

    // 创建UI组件
    createActions();
    ui = new Ui::MainWindow();
    ui->setupUi(this);
    // 设置 Logo pixmap（.ui 中无法表达运行时缩放）
    ui->logoLabel->setPixmap(QPixmap(":/icons/app.svg").scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    // 便捷指针 — 保持现有业务代码中 m_navPanel/m_connectionPanel 访问不变
    m_navPanel = ui->navPanel;
    m_connectionPanel = ui->connectionPanel;
    createStatusBar();  // 必须在 setupUi 之后调用 — statusBar() 需要返回 .ui 中创建的状态栏
    createSystemTrayIcon();

    // 创建核心基础设施（DI 注入链的起点）
    m_threadManager = new ThreadManager(this);
    m_queueManager = new QueueManager(this);
    m_queueManager->initialize(ProcessingConstants::MaxQueueSize);

    // 创建 ServerService — 管理全部服务端生命周期
    m_serverService = new ServerService(m_threadManager, m_queueManager, this);
    connect(m_serverService, &ServerService::listening,
            this, &MainWindow::onServerStarted);
    connect(m_serverService, &ServerService::stopped,
            this, &MainWindow::onServerStopped);
    connect(m_serverService, &ServerService::errorOccurred,
            this, &MainWindow::onServerError);
    connect(m_serverService, &ServerService::clientConnected,
            this, &MainWindow::onClientConnected);
    connect(m_serverService, &ServerService::clientDisconnected,
            this, &MainWindow::onClientDisconnected);
    connect(m_serverService, &ServerService::clientAuthenticated,
            this, &MainWindow::onClientAuthenticated);

    // 设置连接
    setupConnections();

    // 预创建设置对话框，避免首次点击时 UI 线程阻塞
    // （样式表解析 + 密码解密等操作集中在启动阶段完成）
    auto *autoStartMgr = new AutoStartManager(this);
    m_settingsDialog = new SettingsDialog(m_settings, autoStartMgr, this);
    m_settingsDialog->hide();

    // 加载设置
    loadSettings();

    // 设置窗口属性
    setWindowTitle(tr("极域桌面"));
    setFixedSize(960, 680);
    // Qt::Window 在 Windows 上无论如何都会包含 WS_MAXIMIZEBOX,
    // 需通过 showEvent 中的 Win32 API 延迟移除 (见 showEvent 实现)
    setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint
                   | Qt::WindowCloseButtonHint
                   | Qt::MSWindowsFixedSizeDialogHint);

    // --- 主题样式加载 ---
    applyTheme();

    // 显式调用 retranslateUi 确保首次启动时所有子控件的 tr() 生效
    // （LanguageChange 事件在 MainWindow 构造之前的 installTranslator 阶段已发送）
    retranslateUi();

    // 延迟启动服务器
    QTimer::singleShot(500, this, &MainWindow::startServer);
}

MainWindow::~MainWindow() {
    qCDebug(lcUIMainWindow) << "MainWindow::~MainWindow() - Destructor started";

    // 在析构函数中进行最后的资源清理
    // 注意：此时不应该再调用可能触发信号的方法

    // 1. 断开所有信号连接，防止在析构过程中触发信号
    delete ui;

    // 断开并清理所有客户端会话
    for (auto* session : m_clientSessions) {
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

// createActions/createStatusBar/createSystemTrayIcon 实现见 MainWindowLayout.cpp

void MainWindow::setupConnections() {
    // 快捷键全局连接（无系统托盘时仍有效）
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
            if (m_connectionPanel) {
                ConnectionParams params;
                params.host     = host;
                params.port     = port;
                params.hostname = hostname;
                // username/password 从历史中补充
                const HistoryEntry entry = m_connectionPanel->entryFor(host, port);
                params.username = entry.params.username;
                params.password = entry.params.password;
                params.fullScreen     = entry.params.fullScreen;
                params.windowWidth    = entry.params.windowWidth;
                params.windowHeight   = entry.params.windowHeight;
                params.colorDepth     = entry.params.colorDepth;
                params.imageQuality   = entry.params.imageQuality;
                params.viewOnly       = entry.params.viewOnly;
                params.shareClipboard = entry.params.shareClipboard;
                params.showCursor     = entry.params.showCursor;
                params.connectionTimeout   = entry.params.connectionTimeout;
                params.autoReconnect       = entry.params.autoReconnect;
                params.reconnectInterval   = entry.params.reconnectInterval;
                connectToHostDirectly(params);
            }
        });

        connect(m_connectionPanel, &ConnectionPanel::editRequested,
                this, [this](const QString &host, int port) {
            if (!m_connectionPanel)
                return;
            const HistoryEntry entry = m_connectionPanel->entryFor(host, port);
            if (!m_connectionDialog) {
                m_connectionDialog = new ConnectionDialog(this);
                TitleBarTheme::apply(m_connectionDialog, m_themeMode == "dark");
            }
            // 回填全部配置字段
            m_connectionDialog->setConnectionParams(entry.params);
            // 编辑模式：禁止查看已保存的明文密码
            m_connectionDialog->setEditingMode(true);

            if (m_connectionDialog->exec() == QDialog::Accepted) {
                ConnectionParams np = m_connectionDialog->getConnectionParams();
                m_connectionPanel->removeEntry(host, port);
                m_connectionPanel->addEntry(np);
                m_connectionPanel->saveHistory(*m_settings);
                connectToHostDirectly(np);
            }
        });
    }

    // --- 导航面板信号连接 ---
    connect(m_navPanel, &NavPanel::newConnection,
            this, &MainWindow::newConnection);

    connect(m_navPanel, &NavPanel::openSettings,
            this, &MainWindow::showSettings);
    connect(m_navPanel, &NavPanel::showAbout,
            this, &MainWindow::showAbout);
    connect(m_navPanel, &NavPanel::themeToggled,
            this, &MainWindow::toggleTheme);

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

    // 服务器由构造函数末尾统一启动，此处不重复调用
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

    // 如果启用了"关闭时隐藏到托盘"，则隐藏窗口而非退出
    if (m_settings->getBool("UI/closeToTray", false)
        && m_trayIcon && m_trayIcon->isVisible()) {
        qCInfo(lcUIMainWindow) << "MainWindow::closeEvent() - Close to tray enabled, hiding window";
        m_isShuttingDown = false;
        hide();
        event->ignore();
        return;
    }

    // 保存设置
    saveSettings();

    // 执行优雅停止序列
    gracefulShutdown();

    qCInfo(lcUIMainWindow) << "MainWindow::closeEvent() - Server stopped";

    // 接受关闭事件
    event->accept();

    qCInfo(lcUIMainWindow) << "MainWindow::closeEvent() - Close event complete";
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if ( event->type() == QEvent::LanguageChange ) {
        qCInfo(lcUIMainWindow) << "MainWindow::changeEvent - LanguageChange received, calling retranslateUi";
        retranslateUi();
        qCInfo(lcUIMainWindow) << "MainWindow::changeEvent - retranslateUi completed";
    }
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    // Qt::Window 在 Windows 上强制包含 WS_MAXIMIZEBOX，Qt 的 flag 机制无法阻止。
    // 唯一可靠的方案：等 Qt 完成全部窗口初始化后，通过 Win32 API 直接清除该样式位。
    // QTimer::singleShot(0) 确保此代码在 Qt 的所有窗口创建流程执行完毕后运行。
#ifdef Q_OS_WIN
    QTimer::singleShot(0, this, [this]() {
        if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
            SetWindowLongPtr(hwnd, GWL_STYLE,
                             GetWindowLongPtr(hwnd, GWL_STYLE) & ~WS_MAXIMIZEBOX);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    });
#endif

    // 窗口首次显示后，DWM 标题栏属性可能在 Qt 内部窗口创建过程中被重置，
    // 因此需要在 showEvent 中再次应用标题栏主题
    const bool isDark = (m_themeMode == "dark");
    TitleBarTheme::apply(this, isDark);
    TitleBarTheme::apply(m_settingsDialog, isDark);
}

void MainWindow::retranslateUi() {
    qCInfo(lcUIMainWindow) << "MainWindow::retranslateUi - starting UI retranslation";

    // UIC 生成的重翻译（覆盖 titleLabel 等 .ui 文件定义的所有静态文本）
    ui->retranslateUi(this);

    // 手动重翻译（动态创建的控件 + 运行时设置的文本）

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

    // 导航面板
    if ( m_navPanel ) {
        m_navPanel->retranslateUi();
    }

    qCInfo(lcUIMainWindow) << "MainWindow::retranslateUi - UI retranslation done, windowTitle:" << windowTitle();
}

// 槽函数实现
void MainWindow::newConnection() {
    showConnectionDialog();
}


void MainWindow::startServer() {
    if (m_serverService->isRunning()) {
        QMessageBox::information(this, tr("服务器状态"),
                                 tr("服务器已经在运行中。"));
        return;
    }

#ifdef Q_OS_MACOS
    // macOS 平台：检查辅助功能权限
    if ( !checkMacOSAccessibilityPermission() ) {
        QMessageBox::warning(this, tr("需要辅助功能权限"),
            tr("<p>极域桌面需要<b>辅助功能权限</b>才能模拟鼠标和键盘输入。</p>"
                "<p>请按照以下步骤授予权限：</p>"
                "<ol>"
                "<li>打开<b>系统偏好设置</b></li>"
                "<li>选择<b>安全性与隐私</b></li>"
                "<li>点击<b>隐私</b>标签</li>"
                "<li>在左侧列表中选择<b>辅助功能</b></li>"
                "<li>点击左下角的锁图标解锁</li>"
                "<li>在右侧列表中勾选<b>UltraDesktop</b></li>"
                "</ol>"
                "<p>授予权限后，请重启应用程序。</p>"));
        // 尝试打开系统设置
        requestMacOSAccessibilityPermission();
        return;
    }
#endif

    int port = m_settings->getInt("Server/listenPort",
                                  NetworkConstants::DefaultServerPort);
    m_serverService->start(static_cast<quint16>(port));
}

void MainWindow::showSettings() {
	m_settingsDialog->show();
	m_settingsDialog->raise();
	m_settingsDialog->activateWindow();
}

void MainWindow::showAbout() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("关于极域桌面"));
    dialog.setFixedSize(500, 440);

    auto *mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setContentsMargins(16, 12, 16, 12);

    auto *tabWidget = new QTabWidget(&dialog);

    // ── 标签页 1：关于 ──
    auto *aboutTab = new QWidget(tabWidget);
    auto *aboutLayout = new QVBoxLayout(aboutTab);
    aboutLayout->setContentsMargins(20, 16, 20, 16);

    auto *aboutLabel = new QLabel(tr(
        "<h2>%1 %2</h2>"
        "<p>基于 Qt %3 构建的跨平台远程桌面应用程序。</p>"
        "<p>支持 Windows、macOS 和 Linux 系统之间的远程连接与控制。</p>"
        "<hr>"
        "<p>本软件遵循 <b>MIT License</b> 开源发布。</p>"
        "<p>详情请见程序目录下的 LICENSE 文件。</p>"
    ).arg(tr("极域桌面"), QStringLiteral("1.0"), QString::fromLatin1(qVersion())));
    aboutLabel->setWordWrap(true);
    aboutLabel->setTextFormat(Qt::RichText);
    aboutLayout->addWidget(aboutLabel);
    aboutLayout->addStretch();

    tabWidget->addTab(aboutTab, tr("关于"));

    // ── 标签页 2：开源许可 ──
    auto *licenseTab = new QWidget(tabWidget);
    auto *licenseLayout = new QVBoxLayout(licenseTab);
    licenseLayout->setContentsMargins(0, 0, 0, 0);

    auto *licenseBrowser = new QTextBrowser(licenseTab);
    licenseBrowser->setOpenExternalLinks(true);
    licenseBrowser->setHtml(tr(
        "<h3>第三方开源库许可声明</h3>"

        "<h4>Qt %1 &mdash; LGPLv3</h4>"
        "<p>Copyright &copy; The Qt Company Ltd. and other contributors.</p>"
        "<p>Qt 采用 GNU Lesser General Public License v3 (LGPLv3) 许可。"
        "本应用程序通过动态链接方式使用 Qt 库，符合 LGPLv3 的闭源分发条件。"
        "Qt 源代码可从 <a href='https://download.qt.io'>https://download.qt.io</a> 获取。</p>"
        "<p>LGPLv3 全文：<a href='https://www.gnu.org/licenses/lgpl-3.0.html'>"
        "https://www.gnu.org/licenses/lgpl-3.0.html</a></p>"

        "<h4>OpenSSL 3.x &mdash; Apache License 2.0</h4>"
        "<p>Copyright &copy; The OpenSSL Project Authors. All Rights Reserved.</p>"
        "<p>Licensed under the Apache License 2.0 (the \"License\"); "
        "you may not use this file except in compliance with the License. "
        "You may obtain a copy of the License at "
        "<a href='https://www.apache.org/licenses/LICENSE-2.0'>"
        "https://www.apache.org/licenses/LICENSE-2.0</a></p>"
        "<p>本产品包含由 OpenSSL Project 开发的、用于 OpenSSL Toolkit 的软件"
        "(<a href='https://www.openssl.org/'>https://www.openssl.org/</a>)。</p>"

        "<h4>libjpeg-turbo &mdash; BSD 3-Clause</h4>"
        "<p>Copyright &copy; 2009-2026 D. R. Commander. All Rights Reserved.<br>"
        "Copyright &copy; 2015 Viktor Szathm&aacute;ry. All Rights Reserved.</p>"
        "<p>在满足下列条件的前提下，允许以源代码和二进制形式重新分发和使用（无论是否修改）：</p>"
        "<ul>"
        "<li>源代码的再分发必须保留上述版权声明、本条件列表以及下述免责声明。</li>"
        "<li>二进制形式的再分发必须在文档和/或随分发提供的其他材料中"
        "复现上述版权声明、本条件列表以及下述免责声明。</li>"
        "<li>未经事先书面许可，不得使用 libjpeg-turbo 项目或其贡献者的名称"
        "来背书或推广从本软件衍生的产品。</li>"
        "</ul>"
        "<p>THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "
        "\"AS IS\" ...</p>"

        "<h4>nvJPEG / CUDA Runtime &mdash; NVIDIA 专有许可</h4>"
        "<p>Copyright &copy; NVIDIA Corporation. All Rights Reserved.</p>"
        "<p>nvJPEG 和 CUDA Runtime 库为 NVIDIA Corporation 的专有软件，"
        "受 NVIDIA CUDA Toolkit 最终用户许可协议 (EULA) 约束。"
        "仅可在搭载 NVIDIA GPU 的系统上使用和分发。"
        "详情请参阅：<a href='https://docs.nvidia.com/cuda/eula/index.html'>"
        "https://docs.nvidia.com/cuda/eula/index.html</a></p>"
    ).arg(QString::fromLatin1(qVersion())));
    licenseLayout->addWidget(licenseBrowser);

    tabWidget->addTab(licenseTab, tr("开源许可"));

    mainLayout->addWidget(tabWidget);

    TitleBarTheme::apply(&dialog, m_themeMode == "dark");
    dialog.exec();
}

void MainWindow::exitApplication() {
    // 断开所有客户端连接（先断开 finished 信号，防止 close() 触发 removeOne 修改容器）
    for (auto* session : m_clientSessions) {
        disconnect(session, &RemoteDesktopSession::finished, this, nullptr);
        session->close();
    }
    qDeleteAll(m_clientSessions);
    m_clientSessions.clear();

    // 停止服务端
    if (m_serverService) {
        m_serverService->stop();
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
    // Skip polling when window is not visible (minimized/hidden to tray)
    if (!isVisible())
        return;

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
    // 幂等保护：closeEvent() 调用一次，main() 在 app.exec() 返回后再调用一次。
    // 第二次调用时线程已被第一次的 destroyAllThreads() 销毁，
    // 重复调用 destroyAllThreads() 会导致未定义行为。
    if (m_gracefulShutdownDone.exchange(true)) {
        qCDebug(lcUIMainWindow) << "MainWindow::gracefulShutdown() - Already completed, skipping";
        return;
    }

    qCInfo(lcUIMainWindow) << "MainWindow::gracefulShutdown() - Starting graceful shutdown";

    // 1. 断开所有客户端连接
    if ( !m_clientSessions.isEmpty() ) {
        qCInfo(lcUIMainWindow) << "MainWindow::gracefulShutdown() - Disconnecting all clients";
        for (auto* session : m_clientSessions) {
            disconnect(session, &RemoteDesktopSession::finished, this, nullptr);
            session->close();
        }
        qDeleteAll(m_clientSessions);
        m_clientSessions.clear();
    }

    // 2. 停止服务端
    if (m_serverService) {
        m_serverService->stop();
    }

    // 3. 销毁所有线程
    if ( m_threadManager ) {
        m_threadManager->destroyAllThreads();
        qCInfo(lcUIMainWindow) << "MainWindow::gracefulShutdown() - All threads destroyed";
    }

    // 4. 隐藏系统托盘图标——std::_Exit 跳过 ~MainWindow() 析构，
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
        // ConnectionDialog 是懒加载的，创建后立即应用标题栏主题
        TitleBarTheme::apply(m_connectionDialog, m_themeMode == "dark");
    }

    // 预填默认端口（优先服务端运行端口，否则从 SettingsManager 读取）
    int defaultPort = NetworkConstants::DefaultServerPort;
    if (m_serverService->isRunning()) {
        defaultPort = static_cast<int>(m_serverService->port());
    } else {
        defaultPort = m_settings->getInt("Server/listenPort", NetworkConstants::DefaultServerPort);
    }
    m_connectionDialog->setDefaultPort(defaultPort);

    // 预填用户名
    {
        const QString username = m_settings->getString("Server/username");
        if (!username.isEmpty()) {
            m_connectionDialog->setUsername(username);
        }
    }

    // 新建连接模式：允许查看输入的密码
    m_connectionDialog->setEditingMode(false);

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
        m_clientSessions.removeOne(session);
        session->deleteLater();
        if (m_clientSessions.isEmpty()) {
            onAllConnectionsClosed();
        }
    });

    connect(session, &RemoteDesktopSession::errorOccurred, this, [this](const RdError& err) {
        qCWarning(lcClientSession) << "RemoteDesktopSession error:" << err.logLabel();
        updateConnectionStatus(err.logLabel());
    });

    m_clientSessions.append(session);

    if (m_connectionPanel) {
        m_connectionPanel->addEntry(params);
        m_connectionPanel->saveHistory(*m_settings);
    }

    // 启动会话（内部调用 connectToHost）
    session->start();
}

void MainWindow::onServerStarted(quint16 port) {
    qCInfo(lcApp) << "MainWindow::onServerStarted() called with port:" << port;
    updateServerStatus(tr("服务器启动成功，端口: %1").arg(port));

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

void MainWindow::updateServerStatus(const QString& message) {
    // 检查服务端连接状态
    m_serverStatusLabel->setText(message);
}

void MainWindow::updateConnectionStatus(const QString& message) {
    // 检查Client connect to server的连接状态
    m_connectionStatusLabel->setText(message);
}

void MainWindow::applyTheme()
{
    // 暂停主窗口绘制，将所有 UI 变更合并为单次原子重绘，消除主题切换时的多帧闪烁
    // setUpdatesEnabled(false) 会阻止自身及所有子孙 widget 的 update()
    setUpdatesEnabled(false);

    QString qssFile = (m_themeMode == "dark")
        ? ":/styles/dark.qss"
        : ":/styles/light.qss";

    QFile file(qssFile);
    if (file.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(file.readAll());

        // 混合策略：同时设置 qApp 和 MainWindow 的样式表
        // - qApp->setStyleSheet()：覆盖非 MainWindow 子树的顶层窗口
        //   （QMessageBox、QMenu 弹出、QComboBox 下拉列表等）
        // - setStyleSheet()：直接作用于 MainWindow 子树，修复
        //   QScrollArea 内动态创建控件的 CSS 级联失效问题
        qApp->setStyleSheet(styleSheet);
        setStyleSheet(styleSheet);

        file.close();
    }

    m_settings->setString("UI/theme", m_themeMode);

    IconThemeProvider::setDarkMode(m_themeMode == "dark");

    // 图标已统一为平级 SVG（不区分 light/dark），无需逐个控件刷新图标
    // setStyleSheet 触发的全局重绘会自动以正确背景渲染现有图标

    // 更新 Windows 原生标题栏深色/浅色模式（非 Windows 平台为空操作）
    const bool isDark = (m_themeMode == "dark");
    TitleBarTheme::apply(this, isDark);
    TitleBarTheme::apply(m_settingsDialog, isDark);
    if (m_connectionDialog) {
        TitleBarTheme::apply(m_connectionDialog, isDark);
    }

    // 恢复绘制 — 所有变更一次性呈现
    setUpdatesEnabled(true);
}

void MainWindow::toggleTheme()
{
    m_themeMode = (m_themeMode == "dark") ? "light" : "dark";
    applyTheme();
}

void MainWindow::onAllConnectionsClosed() {
    qCDebug(lcUIMainWindow) << "MainWindow::onAllConnectionsClosed() - All client connections closed, keeping server running";
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