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
    
private slots:
    void startApp()
    {
        if (m_process->state() != QProcess::NotRunning) {
            appendLog("应用程序已在运行中");
            return;
        }
        
        appendLog("启动QtRemoteDesktop应用程序...");
        
        QString program = "./bin/QtRemoteDesktop.app/Contents/MacOS/QtRemoteDesktop";
        m_process->setWorkingDirectory("/Users/chenjiayong/Documents/aicode/TraeAI/QtRemoteDesktop/build");
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
        QString output = m_process->readAllStandardOutput();
        QString errorOutput = m_process->readAllStandardError();
        
        appendLog("\n=== 应用程序输出分析 ===");
        
        // 检查是否包含我们的closeEvent日志
        bool hasCloseEventStart = output.contains("MainWindow::closeEvent() - 开始关闭窗口") ||
                                 errorOutput.contains("MainWindow::closeEvent() - 开始关闭窗口");
        
        bool hasSettingsSaved = output.contains("设置已保存") ||
                               errorOutput.contains("设置已保存");
        
        bool hasServerStopped = output.contains("服务器已停止") ||
                               errorOutput.contains("服务器已停止");
        
        bool hasCloseEventComplete = output.contains("MainWindow::closeEvent() - 窗口关闭完成") ||
                                    errorOutput.contains("MainWindow::closeEvent() - 窗口关闭完成");
        
        bool hasAppExit = output.contains("应用程序即将退出") ||
                         errorOutput.contains("应用程序即将退出");
        
        appendLog("closeEvent开始: " + QString(hasCloseEventStart ? "✅" : "❌"));
        appendLog("设置保存: " + QString(hasSettingsSaved ? "✅" : "❌"));
        appendLog("服务器停止: " + QString(hasServerStopped ? "✅" : "❌"));
        appendLog("closeEvent完成: " + QString(hasCloseEventComplete ? "✅" : "❌"));
        appendLog("应用程序退出: " + QString(hasAppExit ? "✅" : "❌"));
        
        if (!output.isEmpty()) {
            appendLog("\n=== 标准输出 ===");
            appendLog(output);
        }
        
        if (!errorOutput.isEmpty()) {
            appendLog("\n=== 错误输出 ===");
            appendLog(errorOutput);
        }
        
        // 总结
        int passedChecks = (hasCloseEventStart ? 1 : 0) +
                          (hasSettingsSaved ? 1 : 0) +
                          (hasServerStopped ? 1 : 0) +
                          (hasCloseEventComplete ? 1 : 0) +
                          (hasAppExit ? 1 : 0);
        
        appendLog(QString("\n=== 测试结果: %1/5 项检查通过 ===").arg(passedChecks));
        
        if (passedChecks >= 3) {
            appendLog("✅ closeEvent方法基本正常工作");
        } else {
            appendLog("❌ closeEvent方法可能存在问题");
        }
    }
    
    void clearLog()
    {
        m_logOutput->clear();
    }
    
private:
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
        m_logOutput->append(QString("[%1] %2").arg(timestamp, message));
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
    
    return app.exec();
}

#include "TestCloseEvent.moc"