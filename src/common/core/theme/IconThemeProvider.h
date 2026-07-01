#pragma once

#include <QIcon>
#include <QString>

/**
 * @brief 主题感知图标提供器
 *
 * 全局单点管理图标加载路径：根据当前 dark/light 模式
 * 自动路由到 :/icons/light/<name>.svg 或 :/icons/dark/<name>.svg。
 */
class IconThemeProvider
{
public:
    IconThemeProvider() = delete;

    /** 根据当前主题模式加载图标 */
    static QIcon icon(const QString &name)
    {
        const QString theme = s_darkMode ? QStringLiteral("dark") : QStringLiteral("light");
        return QIcon(QStringLiteral(":/icons/%1/%2.svg").arg(theme, name));
    }

    /** 设置深色模式 */
    static void setDarkMode(bool dark) { s_darkMode = dark; }

    /** 查询当前是否为深色模式 */
    static bool isDarkMode() { return s_darkMode; }

private:
    static inline bool s_darkMode = true;  // 默认深色，与 QSettings UI/theme=dark 一致
};
