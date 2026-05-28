#pragma once

#include <QtCore/QString>

class QApplication;

/**
 * @brief 初始化应用程序翻译（启动时调用）
 *
 * 从 Config 读取 language 设置并加载对应的 .qm 文件。
 * 若配置未设置则默认使用 zh_CN。
 */
void initTranslation(QApplication& app);

/**
 * @brief 运行时切换应用程序语言
 *
 * 移除旧翻译器、加载新 .qm 文件、安装新翻译器。
 * 安装后 Qt 会自动向所有顶层窗口发送 QEvent::LanguageChange。
 *
 * @return 实际加载的 locale 字符串（失败时返回空字符串）
 */
QString switchTranslation(QApplication& app, const QString& locale);
