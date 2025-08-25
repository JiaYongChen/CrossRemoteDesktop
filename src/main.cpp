#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleFactory>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include "common/core/logging_categories.h"
#include <QtCore/QTranslator>
#include <QtCore/QLocale>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QSplashScreen>
#include <QtGui/QPixmap>
#include <QtCore/QTimer>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCommandLineOption>
#include <QtCore/QMessageLogger>

#include "common/windows/mainwindow.h"
#include "common/core/logger.h"
#include "common/core/config.h"
#include "common/core/uiconstants.h"
#include "common/core/constants.h"

// 应用程序信息
const QString APP_NAME = "Qt Remote Desktop";
const QString APP_VERSION = "1.0.0";
const QString APP_ORGANIZATION = "QtRemoteDesktop";
const QString APP_DOMAIN = "qtremotedesktop.com";

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
    // app.setAttribute(Qt::AA_EnableHighDpiScaling); // Deprecated in Qt 6
    // app.setAttribute(Qt::AA_UseHighDpiPixmaps);    // Deprecated in Qt 6
}

// 初始化日志系统
void initializeLogging()
{
    // 创建日志目录
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
    QDir().mkpath(logDir);
    
    // 配置日志系统（支持配置覆盖）
    Logger* logger = Logger::instance();
    QString configuredLevel = Config::instance()->value("Logging/level", "debug").toString();
    logger->setLogLevel(Logger::stringToLevel(configuredLevel));
    logger->setLogTargets(Logger::Console | Logger::File);
    logger->setLogFile(logDir + "/qtremotedesktop.log");
    logger->setMaxFileSize(CoreConstants::DEFAULT_MAX_FILE_SIZE);
    logger->setMaxFileCount(CoreConstants::DEFAULT_MAX_FILE_COUNT);
    
    // 应用 Qt 分类日志规则（优先环境变量 QT_LOGGING_RULES，其次配置项 Logging/rules）
    const QByteArray envRules = qgetenv("QT_LOGGING_RULES");
    QString rules = envRules.isEmpty() ? Config::instance()->value("Logging/rules", QString()).toString() : QString::fromUtf8(envRules);
    
    // 如果没有配置规则，设置默认的debug规则
    if (rules.isEmpty()) {
        rules = "*.debug=true\nqt.*.debug=false";
    }
    
    Logger::applyQtLoggingRules(rules);
    
    // 安装Qt消息处理器以捕获qDebug()等消息
    Logger::installMessageHandler();
    
    LOG_INFO("Application started");
    Logger::instance()->infof("Version: %s", APP_VERSION.toStdString().c_str());
    Logger::instance()->infof("Qt Version: %s", qVersion());
    
    // 测试日志输出
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "Logger initialized and message routing verified";
    // 打印有效日志级别与规则，便于诊断
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "Effective log level:" << Logger::levelToString(logger->logLevel());
    if (!rules.isEmpty()) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "Effective QT_LOGGING_RULES:" << rules;
    } else {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcApp) << "Effective QT_LOGGING_RULES: (none)";
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
    
    Logger::instance()->infof("Configuration loaded from: %s", Config::instance()->configFile().toStdString().c_str());
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
    Logger::instance()->infof("Translation loaded: %s", configLocale.toStdString().c_str());
    } else {
    Logger::instance()->warningf("Failed to load translation: %s", configLocale.toStdString().c_str());
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
        LOG_INFO("Custom stylesheet applied");
    } else {
        LOG_WARNING("Failed to load custom stylesheet");
    }
}

int main(int argc, char *argv[])
{
    // 创建应用程序实例
    QApplication app(argc, argv);
    
    // 初始化应用程序
    initializeApplication(app);
    
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
        
        // 设置客户端模式（必须在构造函数完成后立即设置）
        if (clientMode) {
            LOG_INFO("Starting in client mode");
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
                    Logger::instance()->infof("Auto-connecting to %s:%d", host.toStdString().c_str(), port);
                    QTimer::singleShot(1000, [&window, host, port]() {
                        window.connectToHostDirectly(host, port);
                    });
                } else {
                    LOG_WARNING("Invalid port number in connect option");
                }
            } else {
                LOG_WARNING("Invalid format for connect option. Use host:port");
            }
        }
        
        LOG_INFO("Application initialized successfully");
        
        // 运行应用程序
        int result = app.exec();
        
        // 保存配置
        Config::instance()->save();
        
    Logger::instance()->infof("Application exiting with code: %d", result);
        
        return result;
        
    } catch (const std::exception &e) {
        QString errorMsg = QString("Unhandled exception: %1").arg(e.what());
        LOG_CRITICAL(errorMsg);
        
        QMessageBox::critical(nullptr, APP_NAME, 
                            QObject::tr("发生严重错误：%1").arg(e.what()));
        return -1;
        
    } catch (...) {
        QString errorMsg = "Unknown exception occurred";
        LOG_CRITICAL(errorMsg);
        
        QMessageBox::critical(nullptr, APP_NAME, 
                            QObject::tr("发生未知错误，应用程序将退出。"));
        return -1;
    }
}