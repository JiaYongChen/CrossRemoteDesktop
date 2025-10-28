#ifndef UICONSTANTS_H
#define UICONSTANTS_H

#include <QtGui/QColor>

/**
 * @brief UI相关常量定义类
 * 
 * 提供用户界面相关的常量定义，包括窗口尺寸、颜色、动画等。
 * 对于通用的输入、帧率等常量，请参考 CoreConstants。
 */
class UIConstants {
public:
    // ==================== 窗口尺寸常量 ====================
    static const int MIN_WINDOW_WIDTH = 800;              ///< 最小窗口宽度
    static const int MIN_WINDOW_HEIGHT = 600;             ///< 最小窗口高度
    static const int DEFAULT_WINDOW_WIDTH = 1024;         ///< 默认窗口宽度
    static const int DEFAULT_WINDOW_HEIGHT = 768;         ///< 默认窗口高度
    static const int MAIN_WINDOW_WIDTH = 1200;            ///< 主窗口宽度
    static const int MAIN_WINDOW_HEIGHT = 800;            ///< 主窗口高度
    static const int CONNECTION_DIALOG_WIDTH = 400;       ///< 连接对话框宽度
    static const int CONNECTION_DIALOG_HEIGHT = 300;      ///< 连接对话框高度

    // ==================== 显示分辨率常量 ====================
    static const int MAX_DISPLAY_WIDTH = 1920;            ///< 最大显示宽度
    static const int MAX_DISPLAY_HEIGHT = 1080;           ///< 最大显示高度

    // ==================== 历史记录常量 ====================
    static const int MAX_CONNECTION_HISTORY = 20;         ///< 最大连接历史记录数

    // ==================== 会话管理常量 ====================
    static const int STATS_UPDATE_INTERVAL = 500;         ///< 统计更新间隔 500ms
    static const int MAX_FRAME_HISTORY = 60;              ///< 保留60帧的时间记录

    // ==================== 输入处理常量（UI特定） ====================
    static const int DEFAULT_INPUT_BUFFER_SIZE = 100;     ///< 输入缓冲区大小
    static const int DEFAULT_INPUT_FLUSH_INTERVAL = 10;   ///< 输入刷新间隔 10ms
    static const int MAX_PROCESSING_TIMES_HISTORY = 1000; ///< 保留1000次处理时间记录

    // ==================== 服务器端口常量 ====================
    static const int DEFAULT_SERVER_PORT = 5901;          ///< 默认服务器端口（避免与VNC 5900冲突）
    static const int MIN_SERVER_PORT = 1024;              ///< 最小端口号
    static const int MAX_SERVER_PORT = 65535;             ///< 最大端口号

    // ==================== 颜色常量 ====================
    static const QColor LIGHT_GRAY_COLOR;                 ///< 浅灰色
    static const QColor WHITE_COLOR;                      ///< 白色
    static const QColor MEDIUM_GRAY_COLOR;                ///< 中灰色

    // ==================== 动画常量 ====================
    static const double PULSE_MULTIPLIER;                 ///< 脉冲倍数
    static const double PULSE_DIVISOR;                    ///< 脉冲除数
    static const int PULSE_BASE_ALPHA;                    ///< 脉冲基础透明度
    static const int PULSE_AMPLITUDE;                     ///< 脉冲振幅

private:
    UIConstants() = delete; // 禁止实例化
};

// ==================== 内联定义颜色常量 ====================
inline const QColor UIConstants::LIGHT_GRAY_COLOR(200, 200, 200);
inline const QColor UIConstants::WHITE_COLOR(255, 255, 255);
inline const QColor UIConstants::MEDIUM_GRAY_COLOR(150, 150, 150);

// ==================== 内联定义动画常量 ====================
inline const double UIConstants::PULSE_MULTIPLIER = 100.0;
inline const double UIConstants::PULSE_DIVISOR = 30.0;
inline const int UIConstants::PULSE_BASE_ALPHA = 100;
inline const int UIConstants::PULSE_AMPLITUDE = 100;

#endif // UICONSTANTS_H