#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleFactory>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include "common/core/logging/LoggingCategories.h"
#include <QtCore/QTranslator>
#include <QtCore/QLocale>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QSplashScreen>
#include <QtGui/QPixmap>
#include <QtCore/QTimer>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCommandLineOption>
#include <QtCore/QMessageLogger>
#include <signal.h>
#include <unistd.h>

#include "common/windows/MainWindow.h"
#include "common/core/config/Config.h"
#include "common/core/config/UiConstants.h"
#include "common/core/config/Constants.h"

// 应用程序信息
const QString APP_NAME = "Qt Remote Desktop";
const QString APP_VERSION = "1.0.0";
const QString APP_ORGANIZATION = "QtRemoteDesktop";
const QString APP_DOMAIN = "qtremotedesktop.com";

// 全局变量用于信号处理
static MainWindow* g_mainWindow = nullptr;

// 信号处理器函数
void signalHandler(int signal)
{
    qCInfo(lcApp, "收到信号: %d", signal);
    
    if (g_mainWindow) {
        qCInfo(lcApp, "通过closeEvent正常关闭应用程序");
        // 通过调用close()来触发closeEvent
        QTimer::singleShot(0, g_mainWindow, &QWidget::close);
    } else {
        qCWarning(lcApp, "主窗口指针为空，直接退出应用程序");
        QApplication::quit();
    }
}

// 安装信号处理器
void installSignalHandlers()
{
    qCInfo(lcApp, "安装信号处理器");
    
    // 安装SIGTERM和SIGINT信号处理器
    signal(SIGTERM, signalHandler);
    signal(SIGINT, signalHandler);
    
    qCInfo(lcApp, "信号处理器安装完成");
}

// 初始化应用程序设置
void initializeApplication(QApplication &app)
{
    // 设置应用程序信息
    app.setApplicationName(APP_NAME);
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName(APP_ORGANIZATION);
    app.setOrganizationDomain(APP_DOMAIN);
    
    // 设置应用程序图标
    app.setWindowIcon(QIcon(":/icons/app.svg"));
    
    // 设置高DPI支持
    // High-DPI scaling is enabled by default in Qt 6
}

// 初始化日志系统
void initializeLogging()
{
    // 创建日志目录
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
    QDir().mkpath(logDir);
    
    // 配置日志系统（支持配置覆盖）
    // Logger已迁移到QLoggingCategory
    QString configuredLevel = Config::instance()->value("Logging/level", "debug").toString();
    // QLoggingCategory不支持动态设置级别和文件输出
    // 日志文件输出需要通过自定义消息处理器实现
    
    // 应用 Qt 分类日志规则（优先环境变量 QT_LOGGING_RULES，其次配置项 Logging/rules）
    const QByteArray envRules = qgetenv("QT_LOGGING_RULES");
    QString rules = envRules.isEmpty() ? Config::instance()->value("Logging/rules", QString()).toString() : QString::fromUtf8(envRules);
    
    // 如果没有配置规则，设置默认的debug规则
    if (rules.isEmpty()) {
        rules = "*.debug=true\nqt.*.debug=false";
    }
    
    QLoggingCategory::setFilterRules(rules);
    
    // 安装Qt消息处理器以捕获qDebug()等消息
    // QLoggingCategory自动处理消息;
    
    qCInfo(lcApp, "Application started");
    qCInfo(lcApp, "Version: %s", APP_VERSION.toStdString().c_str());
    qCInfo(lcApp, "Qt Version: %s", qVersion());
    
    // 测试日志输出
    qCInfo(lcApp, "Logger initialized and message routing verified");
    // 打印有效日志级别与规则，便于诊断
    if (!rules.isEmpty()) {
        qCInfo(lcApp, "Effective QT_LOGGING_RULES: %s", rules.toStdString().c_str());
    } else {
        qCInfo(lcApp, "Effective QT_LOGGING_RULES: (none)");
    }
}

// 初始化配置系统
void initializeConfig()
{
    // 创建配置目录
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    
    // 初始化配置
    Config::instance()->setConfigFile(configDir + "/settings.ini");
    Config::instance()->load();
    
    qCInfo(lcApp, "Configuration loaded from: %s", Config::instance()->configFile().toStdString().c_str());
}

// 加载翻译文件
void loadTranslations(QApplication &app)
{
    QTranslator *translator = new QTranslator(&app);
    
    // 默认使用中文
    QString defaultLocale = "zh_CN";
    
    // 从配置中获取语言设置
    QString configLocale = Config::instance()->value("general/language", defaultLocale).toString();
    
    // 加载翻译文件
    QString translationFile = QString(":/translations/%1.qm").arg(configLocale);
    if (translator->load(translationFile)) {
        app.installTranslator(translator);
    qCInfo(lcApp, "Translation loaded: %s", configLocale.toStdString().c_str());
    } else {
    qCWarning(lcApp, "Failed to load translation: %s", configLocale.toStdString().c_str());
    }
}

// 应用样式
void applyStyles(QApplication &app)
{
    // 设置应用程序样式
    QString styleName = Config::instance()->value("general/style", "Fusion").toString();
    app.setStyle(QStyleFactory::create(styleName));
    
    // 加载自定义样式表
    QFile styleFile(":/styles/default.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(styleFile.readAll());
        app.setStyleSheet(styleSheet);
        qCInfo(lcApp, "Custom stylesheet applied");
    } else {
        qCWarning(lcApp, "Failed to load custom stylesheet");
    }
}

int main(int argc, char *argv[])
{
    // 创建应用程序实例
    QApplication app(argc, argv);
    
    // 初始化应用程序
    initializeApplication(app);
    
    // 新增：防止关闭最后一个窗口时自动退出应用
    // 背景说明：
    // - Qt 默认行为为当最后一个顶层窗口关闭时自动退出（quitOnLastWindowClosed = true）。
    // - 由于远程桌面窗口（ClientRemoteWindow）是独立顶层窗口，在主窗口被隐藏到托盘或暂未显示的情况下，
    //   用户关闭远程桌面窗口可能被视为“最后一个窗口关闭”，从而导致整个应用意外退出。
    // - 为避免该问题，这里显式禁用该自动退出行为，确保关闭远程桌面窗口不会导致应用退出。
    app.setQuitOnLastWindowClosed(false);
    qCInfo(lcApp, "setQuitOnLastWindowClosed(false) applied to prevent auto-quit when closing last window");
    
    // 解析命令行参数
    QCommandLineParser parser;
    parser.setApplicationDescription("Qt Remote Desktop - 远程桌面应用程序");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption connectOption(QStringList() << "c" << "connect",
                                    "自动连接到指定的主机",
                                    "host:port");
    parser.addOption(connectOption);
    
    QCommandLineOption clientModeOption(QStringList() << "client",
                                       "以客户端模式启动（不启动服务器）");
    parser.addOption(clientModeOption);
    
    parser.process(app);
    
    try {
        // 初始化日志系统
        initializeLogging();
        
        // 初始化配置系统
        initializeConfig();
        
        // 加载翻译
        loadTranslations(app);
        
        // 应用样式
        applyStyles(app);
        
        // 检查是否为客户端模式
        bool clientMode = parser.isSet(clientModeOption);
        
        // 创建主窗口
        MainWindow window;
        
        // 设置全局指针用于信号处理
        g_mainWindow = &window;
        
        // 安装信号处理器
        installSignalHandlers();
        
        // 设置客户端模式（必须在构造函数完成后立即设置）
        if (clientMode) {
            qCInfo(lcApp, "Starting in client mode");
            window.setClientMode(true);
        } else {
            // 默认服务器模式，启动服务器
            window.setClientMode(false);
        }
        
        window.show();
        
        // 检查自动连接参数
        if (parser.isSet(connectOption)) {
            QString connectTo = parser.value(connectOption);
            QStringList parts = connectTo.split(':');
            if (parts.size() == 2) {
                QString host = parts[0];
                bool ok;
                int port = parts[1].toInt(&ok);
                if (ok && port > 0 && port <= 65535) {
                    qCInfo(lcApp, "Auto-connecting to %s:%d", host.toStdString().c_str(), port);
                    QTimer::singleShot(1000, [&window, host, port]() {
                        window.connectToHostDirectly(host, port);
                    });
                } else {
                    qCWarning(lcApp, "Invalid port number in connect option");
                }
            } else {
                qCWarning(lcApp, "Invalid format for connect option. Use host:port");
            }
        }
        
        qCInfo(lcApp, "Application initialized successfully");
        
        // 运行应用程序
        int result = app.exec();
        
        // 清理全局指针
        g_mainWindow = nullptr;
        
        // 保存配置
        Config::instance()->save();
        
        qCInfo(lcServer, "Application exiting with code: %d", result);
        
        return result;
        
    } catch (const std::exception &e) {
        QString errorMsg = QString("Unhandled exception: %1").arg(e.what());
        qCCritical(lcApp, "%s", errorMsg.toStdString().c_str());
        
        QMessageBox::critical(nullptr, APP_NAME, 
                            QObject::tr("发生严重错误：%1").arg(e.what()));
        return -1;
        
    } catch (...) {
        QString errorMsg = "Unknown exception occurred";
        qCCritical(lcApp, "%s", errorMsg.toStdString().c_str());
        
        QMessageBox::critical(nullptr, APP_NAME, 
                            QObject::tr("发生未知错误，应用程序将退出。"));
        return -1;
    }
}