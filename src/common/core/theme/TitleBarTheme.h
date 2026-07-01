#pragma once

class QWidget;

/**
 * @brief Windows 原生标题栏主题工具
 *
 * 调用 DWM API 设置窗口标题栏的深色/浅色模式。
 * 在非 Windows 平台上为空操作，调用安全无副作用。
 *
 * 使用示例：
 * @code
 *   TitleBarTheme::apply(this, true);   // 深色标题栏
 *   TitleBarTheme::apply(dialog, false); // 浅色标题栏
 * @endcode
 */
namespace TitleBarTheme {

/**
 * @brief 将顶层窗口的原生标题栏设置为深色或浅色模式
 * @param window 目标顶层窗口（必须是 top-level widget，非顶层窗口会被静默忽略）
 * @param dark   true = 深色标题栏, false = 浅色标题栏
 */
void apply(QWidget *window, bool dark);

} // namespace TitleBarTheme
