#pragma once

/**
 * @brief UI相关常量定义类
 *
 * 提供用户界面相关的常量定义，包括窗口尺寸等。
 * 对于通用的输入、帧率等常量，请参考 CoreConstants。
 */
class UIConstants {
public:
    // ==================== 窗口尺寸常量 ====================
    static const int MIN_WINDOW_WIDTH = 800;              ///< 最小窗口宽度
    static const int MIN_WINDOW_HEIGHT = 600;             ///< 最小窗口高度
    static const int MAIN_WINDOW_WIDTH = 1200;            ///< 主窗口宽度
    static const int MAIN_WINDOW_HEIGHT = 800;            ///< 主窗口高度

    // ==================== 会话管理常量 ====================
    static const int STATS_UPDATE_INTERVAL = 500;         ///< 统计更新间隔 500ms

    // ==================== 服务器端口常量 ====================
    static const int DEFAULT_SERVER_PORT = 5921;          ///< 默认监听端口

private:
    UIConstants() = delete; // 禁止实例化
};
