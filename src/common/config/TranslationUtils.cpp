#include "TranslationUtils.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QLibraryInfo>
#include <QtCore/QStringList>
#include <QtCore/QTranslator>
#include <QtWidgets/QApplication>

#include "common/config/SettingsManager.h"
#include "common/logging/LoggingCategories.h"

// 应用翻译器（app .qm）：由 initTranslation / switchTranslation 管理生命周期
static QTranslator* s_appTranslator = nullptr;

// Qt 基础翻译器（qtbase .qm）：为 QDialogButtonBox 等标准 Qt 控件提供中文翻译
static QTranslator* s_qtBaseTranslator = nullptr;

// 优先加载随程序部署的翻译，开发构建再回退到编译期注入的目录。
static QStringList appTranslationsDirs() {
    QStringList directories {
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations"))
    };
#ifdef RD_TRANSLATIONS_DIR
    directories.append(QStringLiteral(RD_TRANSLATIONS_DIR));
#endif
    directories.append(QCoreApplication::applicationDirPath());
    directories.removeDuplicates();
    return directories;
}

/** 安装或卸载 Qt 基础翻译器（仅非英文时需要） */
static void manageQtBaseTranslation(QApplication& app, const QString& locale) {
    if (s_qtBaseTranslator) {
        app.removeTranslator(s_qtBaseTranslator);
        delete s_qtBaseTranslator;
        s_qtBaseTranslator = nullptr;
    }

    // 英文是 Qt 默认语言，不需要 qtbase 翻译
    if (locale == QStringLiteral("en_US")) {
        return;
    }

    // 部署的 Qt DLL 不一定能定位开发机安装目录，先查找随程序分发的 qtbase 翻译。
    s_qtBaseTranslator = new QTranslator(&app);
    QStringList directories = appTranslationsDirs();
    directories.append(QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    directories.removeDuplicates();
    for (const QString& directory : directories) {
        if (s_qtBaseTranslator->load(QStringLiteral("qtbase_%1").arg(locale), directory)) {
            // 先安装 qtbase，再安装 app 翻译器 → app 翻译优先级更高
            app.installTranslator(s_qtBaseTranslator);
            qCInfo(lcApp) << "Qt base translation loaded for:" << locale << "from" << directory;
            return;
        }
    }
    qCWarning(lcApp) << "Failed to load qtbase translation:" << locale << "from" << directories;
    delete s_qtBaseTranslator;
    s_qtBaseTranslator = nullptr;
}

/** 加载并安装指定 locale 的应用翻译器；失败返回 nullptr */
static QTranslator* loadAppTranslator(QApplication& app, const QString& locale) {
    auto* translator = new QTranslator(&app);
    const QStringList directories = appTranslationsDirs();
    for (const QString& directory : directories) {
        if (translator->load(QStringLiteral("%1.qm").arg(locale), directory)) {
            app.installTranslator(translator);
            return translator;
        }
    }
    qCWarning(lcApp) << "Failed to load translation:" << locale
                     << "from" << directories;
    delete translator;
    return nullptr;
}

void initTranslation(QApplication& app, SettingsManager &settings) {
    const QString defaultLocale = QStringLiteral("zh_CN");
    const QString configLocale = settings.getString("General/language", defaultLocale);

    // 先安装 qtbase（低优先级），再安装 app 翻译（高优先级）
    manageQtBaseTranslation(app, configLocale);

    s_appTranslator = loadAppTranslator(app, configLocale);
    if (s_appTranslator) {
        qCInfo(lcApp) << "Translation loaded:" << configLocale;
    }
}

QString switchTranslation(QApplication& app, const QString& locale) {
    qCInfo(lcApp) << "switchTranslation called, locale:" << locale;

    if (locale.isEmpty()) {
        qCWarning(lcApp) << "switchTranslation: empty locale";
        return {};
    }

    // 移除并销毁旧 app 翻译器
    if (s_appTranslator) {
        app.removeTranslator(s_appTranslator);
        delete s_appTranslator;
        s_appTranslator = nullptr;
        qCInfo(lcApp) << "switchTranslation: old translator removed";
    }

    // 更新 qtbase 翻译器
    manageQtBaseTranslation(app, locale);

    // 加载新 app 翻译器
    s_appTranslator = loadAppTranslator(app, locale);
    if (s_appTranslator) {
        qCInfo(lcApp) << "switchTranslation: new translator installed, LanguageChange events dispatched";
        return locale;
    }
    return {};
}
