#pragma once

#include <QIcon>
#include <QString>

#include "common/core/logging/LoggingCategories.h"

/**
 * @brief 图标加载提供器
 *
 * 全局单点管理图标加载路径：统一路由到 :/icons/<name>.svg。
 */
class IconThemeProvider
{
public:
    IconThemeProvider() = delete;

    /** 加载指定名称的图标 */
    static QIcon icon(const QString &name)
    {
        const QString path = QStringLiteral(":/icons/%1.svg").arg(name);
        QIcon icon(path);
        if (icon.isNull())
            qCDebug(lcUI) << "图标加载失败:" << path;
        return icon;
    }

    /** 设置深色模式（保留，供外部调用方使用） */
    static void setDarkMode(bool dark) { s_darkMode = dark; }

    /** 查询当前是否为深色模式（保留，供 TitleBarTheme 等使用） */
    static bool isDarkMode() { return s_darkMode; }

private:
    static inline bool s_darkMode = true;  // 默认深色，与 QSettings UI/theme=dark 一致
};
