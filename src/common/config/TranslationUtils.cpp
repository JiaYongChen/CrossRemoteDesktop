#include "TranslationUtils.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QLibraryInfo>
#include <QtCore/QTranslator>
#include <QtWidgets/QApplication>

#include "common/config/SettingsManager.h"
#include "common/logging/LoggingCategories.h"

// 应用翻译器（app .qm）：由 initTranslation / switchTranslation 管理生命周期
static QTranslator* s_appTranslator = nullptr;

// Qt 基础翻译器（qtbase .qm）：为 QDialogButtonBox 等标准 Qt 控件提供中文翻译
static QTranslator* s_qtBaseTranslator = nullptr;

// 应用翻译目录：编译期由 CMake 注入（build/translations），运行时文件加载。
// 未定义时（如独立测试构建）回退到可执行文件同目录。
static QString appTranslationsDir() {
#ifdef RD_TRANSLATIONS_DIR
    return QStringLiteral(RD_TRANSLATIONS_DIR);
#else
    return QCoreApplication::applicationDirPath();
#endif
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

    // qtbase 翻译为 Qt 安装自带，运行时从 Qt translations 目录定位（无需随包分发）
    s_qtBaseTranslator = new QTranslator(&app);
    const QString qtTranslationsDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (s_qtBaseTranslator->load(QStringLiteral("qtbase_%1").arg(locale), qtTranslationsDir)) {
        // 先安装 qtbase，再安装 app 翻译器 → app 翻译优先级更高
        app.installTranslator(s_qtBaseTranslator);
        qCInfo(lcApp) << "Qt base translation loaded for:" << locale;
    } else {
        qCWarning(lcApp) << "Failed to load qtbase translation:" << locale
                         << "from" << qtTranslationsDir;
        delete s_qtBaseTranslator;
        s_qtBaseTranslator = nullptr;
    }
}

/** 加载并安装指定 locale 的应用翻译器；失败返回 nullptr */
static QTranslator* loadAppTranslator(QApplication& app, const QString& locale) {
    auto* translator = new QTranslator(&app);
    if (translator->load(QStringLiteral("%1.qm").arg(locale), appTranslationsDir())) {
        app.installTranslator(translator);
        return translator;
    }
    qCWarning(lcApp) << "Failed to load translation:" << locale
                     << "from" << appTranslationsDir();
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
