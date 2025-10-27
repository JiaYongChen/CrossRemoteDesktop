/**
 * @file test_close_event.cpp
 * @brief 测试MainWindow的closeEvent方法的C++程序
 * 
 * 这个程序会启动QtRemoteDesktop应用程序，然后通过Qt信号机制
 * 触发正常的关闭流程，验证closeEvent方法是否正确执行。
 */

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtCore/QTimer>
#include <QtCore/QProcess>
#include <QtCore/QDebug>
#include <QtCore/QThread>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTextEdit>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>

class TestWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit TestWindow(QWidget *parent = nullptr)
        : QMainWindow(parent)
        , m_process(nullptr)
        , m_logOutput(nullptr)
    {
        setupUI();
        setupProcess();
    }
    
    ~TestWindow()
    {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(3000);
        }
    }
    
public slots:
    void startApp()
    {
        if (m_process->state() != QProcess::NotRunning) {
            appendLog("应用程序已在运行中");
            return;
        }
        
        appendLog("启动QtRemoteDesktop应用程序...");
        QString program = locateProgram();
        if (program.isEmpty()) {
            appendLog("❌ 未找到QtRemoteDesktop可执行文件，请检查构建输出");
            return;
        }
        appendLog("使用可执行路径: " + program);
        // 使用当前工作目录（由CTest配置），不再硬编码
        m_process->start(program);
        
        if (!m_process->waitForStarted(5000)) {
            appendLog("❌ 启动失败: " + m_process->errorString());
        } else {
            appendLog("✅ 应用程序已启动，PID: " + QString::number(m_process->processId()));
        }
    }
    
    void stopApp()
    {
        if (m_process->state() == QProcess::NotRunning) {
            appendLog("应用程序未在运行");
            return;
        }
        
        appendLog("发送终止信号给应用程序...");
        m_process->terminate();
        
        // 等待应用程序关闭
        if (m_process->waitForFinished(10000)) {
            appendLog("✅ 应用程序已正常关闭，退出码: " + QString::number(m_process->exitCode()));
        } else {
            appendLog("❌ 应用程序未能在10秒内关闭，强制终止");
            m_process->kill();
            m_process->waitForFinished(3000);
        }
    }
    
    void testCloseEvent()
    {
        appendLog("\n=== 开始测试closeEvent方法 ===");
        
        // 启动应用程序
        startApp();
        
        // 等待应用程序完全启动
        QTimer::singleShot(3000, this, [this]() {
            appendLog("等待3秒后发送关闭信号...");
            
            // 发送SIGTERM信号，这会触发Qt的正常关闭流程
            stopApp();
            
            // 分析输出
            QTimer::singleShot(2000, this, [this]() {
                analyzeOutput();
            });
        });
    }
    
    void analyzeOutput()
    {
        // 合并通道读取，确保捕获所有输出（已设置MergedChannels，仅读取标准输出即可）
        QString output = m_process->readAllStandardOutput();
        
        appendLog("\n=== 应用程序输出分析 ===");
        
        // 检查是否包含我们的closeEvent日志
        bool hasCloseEventStart = output.contains("MainWindow::closeEvent() - 开始关闭窗口");
        
        bool hasSettingsSaved = output.contains("设置已保存");
        
        // 更严格：最终态
        bool hasServerStoppedFinal = output.contains("服务器已停止");
        // 辅助态：信息性日志
        bool hasServerStoppedAux = output.contains("TCP服务器已停止") ||
                                   output.contains("服务器已在关闭过程中");
        
        bool hasCloseEventComplete = output.contains("MainWindow::closeEvent() - 窗口关闭完成");
        
        bool hasAppExit = output.contains("应用程序即将退出");
        
        appendLog("closeEvent开始: " + QString(hasCloseEventStart ? "✅" : "❌"));
        appendLog("设置保存: " + QString(hasSettingsSaved ? "✅" : "❌"));
        appendLog("服务器停止(最终态): " + QString(hasServerStoppedFinal ? "✅" : "❌"));
        appendLog("服务器停止(辅助态): " + QString(hasServerStoppedAux ? "✅" : "❌"));
        appendLog("closeEvent完成: " + QString(hasCloseEventComplete ? "✅" : "❌"));
        appendLog("应用程序退出: " + QString(hasAppExit ? "✅" : "❌"));
        
        if (!output.isEmpty()) {
            appendLog("\n=== 标准输出 ===");
            appendLog(output);
        }
        
        // 错误输出已合并到标准输出
        
        // 总结
        int passedChecks = (hasCloseEventStart ? 1 : 0) +
                          (hasSettingsSaved ? 1 : 0) +
                          (hasServerStoppedFinal ? 1 : 0) +
                          (hasServerStoppedAux ? 1 : 0) +
                          (hasCloseEventComplete ? 1 : 0) +
                          (hasAppExit ? 1 : 0);
        
        appendLog(QString("\n=== 测试结果: %1/6 项检查通过 ===").arg(passedChecks));
        
        // 关键判定（更严格）：必须检测到最终态“服务器已停止”，且至少有一次closeEvent日志
        const bool criticalCloseEvent = hasCloseEventStart || hasCloseEventComplete;
        const bool criticalServerStopped = hasServerStoppedFinal;
        const bool passed = (criticalServerStopped && criticalCloseEvent);
        if (passed) {
            appendLog("✅ closeEvent方法基本正常工作");
        } else {
            appendLog("❌ closeEvent方法可能存在问题");
        }

        // 分析完成后退出测试程序（用于自动化测试场景），根据结果设置退出码
        int exitCode = passed ? 0 : 1;
        QTimer::singleShot(0, qApp, [exitCode]() { QCoreApplication::exit(exitCode); });
    }
    
    void clearLog()
    {
        m_logOutput->clear();
    }
    
private:
    QString locateProgram()
    {
        // 优先使用环境变量指定的路径
        QString envPath = qEnvironmentVariable("RD_MAIN_APP_PATH");
        if (!envPath.isEmpty() && QFileInfo::exists(envPath)) {
            return QFileInfo(envPath).absoluteFilePath();
        }

        // 基于当前工作目录尝试常见构建输出位置
        QDir cwd = QDir::current();
        QDir appDir(QCoreApplication::applicationDirPath());
        QString projectRoot = QDir(appDir.filePath("../..")).canonicalPath();
        QStringList candidates;

#ifdef Q_OS_MAC
        // .app bundle 可能存在
        candidates << cwd.filePath("../QtRemoteDesktop.app/Contents/MacOS/QtRemoteDesktop");
        candidates << cwd.filePath("../Debug/QtRemoteDesktop.app/Contents/MacOS/QtRemoteDesktop");
        candidates << cwd.filePath("../Release/QtRemoteDesktop.app/Contents/MacOS/QtRemoteDesktop");
        candidates << cwd.filePath("QtRemoteDesktop.app/Contents/MacOS/QtRemoteDesktop");
        // 项目根输出（CMAKE_RUNTIME_OUTPUT_DIRECTORY）
        candidates << QDir(projectRoot).filePath("QtRemoteDesktop.app/Contents/MacOS/QtRemoteDesktop");
        candidates << QDir(projectRoot).filePath("Debug/QtRemoteDesktop.app/Contents/MacOS/QtRemoteDesktop");
        candidates << QDir(projectRoot).filePath("Release/QtRemoteDesktop.app/Contents/MacOS/QtRemoteDesktop");
#endif
        // 非bundle（常见于开发构建）
        candidates << cwd.filePath("../QtRemoteDesktop");
        candidates << cwd.filePath("../Debug/QtRemoteDesktop");
        candidates << cwd.filePath("../Release/QtRemoteDesktop");
        candidates << cwd.filePath("QtRemoteDesktop");
        // 项目根输出（CMAKE_RUNTIME_OUTPUT_DIRECTORY）
        candidates << QDir(projectRoot).filePath("QtRemoteDesktop");
        candidates << QDir(projectRoot).filePath("Debug/QtRemoteDesktop");
        candidates << QDir(projectRoot).filePath("Release/QtRemoteDesktop");

        for (const QString &path : candidates) {
            QFileInfo fi(path);
            if (fi.exists() && fi.isExecutable())
                return fi.absoluteFilePath();
        }
        return QString();
    }

    void setupUI()
    {
        setWindowTitle("QtRemoteDesktop CloseEvent 测试工具");
        setMinimumSize(800, 600);
        
        QWidget *centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);
        
        QVBoxLayout *layout = new QVBoxLayout(centralWidget);
        
        // 标题
        QLabel *titleLabel = new QLabel("QtRemoteDesktop CloseEvent 测试工具", this);
        titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; margin: 10px;");
        layout->addWidget(titleLabel);
        
        // 按钮
        QPushButton *startBtn = new QPushButton("启动应用程序", this);
        QPushButton *stopBtn = new QPushButton("停止应用程序", this);
        QPushButton *testBtn = new QPushButton("测试closeEvent", this);
        QPushButton *clearBtn = new QPushButton("清空日志", this);
        
        connect(startBtn, &QPushButton::clicked, this, &TestWindow::startApp);
        connect(stopBtn, &QPushButton::clicked, this, &TestWindow::stopApp);
        connect(testBtn, &QPushButton::clicked, this, &TestWindow::testCloseEvent);
        connect(clearBtn, &QPushButton::clicked, this, &TestWindow::clearLog);
        
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        buttonLayout->addWidget(startBtn);
        buttonLayout->addWidget(stopBtn);
        buttonLayout->addWidget(testBtn);
        buttonLayout->addWidget(clearBtn);
        layout->addLayout(buttonLayout);
        
        // 日志输出
        QLabel *logLabel = new QLabel("测试日志:", this);
        layout->addWidget(logLabel);
        
        m_logOutput = new QTextEdit(this);
        m_logOutput->setReadOnly(true);
        m_logOutput->setFont(QFont("Consolas", 10));
        layout->addWidget(m_logOutput);
    }
    
    void setupProcess()
    {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        
        connect(m_process, &QProcess::started, this, [this]() {
            appendLog("进程已启动");
        });
        
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
            QString statusStr = (exitStatus == QProcess::NormalExit) ? "正常退出" : "崩溃退出";
            appendLog(QString("进程已结束: %1, 退出码: %2").arg(statusStr).arg(exitCode));
        });
        
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            QString errorStr;
            switch (error) {
                case QProcess::FailedToStart: errorStr = "启动失败"; break;
                case QProcess::Crashed: errorStr = "进程崩溃"; break;
                case QProcess::Timedout: errorStr = "超时"; break;
                case QProcess::WriteError: errorStr = "写入错误"; break;
                case QProcess::ReadError: errorStr = "读取错误"; break;
                default: errorStr = "未知错误"; break;
            }
            appendLog("进程错误: " + errorStr);
        });
    }
    
    void appendLog(const QString &message)
    {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        const QString line = QString("[%1] %2").arg(timestamp, message);
        m_logOutput->append(line);
        qInfo().noquote() << line;
        m_logOutput->ensureCursorVisible();
    }
    
private:
    QProcess *m_process;
    QTextEdit *m_logOutput;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    TestWindow window;
    window.show();

    // 自动运行模式：用于CTest/CI环境，无需人工点击
    const bool autoRun = qEnvironmentVariableIsSet("AUTO_RUN") ||
                         app.arguments().contains("--auto");
    if (autoRun) {
        QTimer::singleShot(200, &window, &TestWindow::testCloseEvent);
    }

    return app.exec();
}

#include "test_close_event.moc"