#ifndef UICONSTANTS_H
#define UICONSTANTS_H

#include <QtGui/QColor>

class UIConstants
{
public:
    // 文件大小常量
    static const int DEFAULT_MAX_FILE_SIZE = 10 * 1024 * 1024; // 10MB
    static const int MAX_FRAME_SIZE = 10 * 1024 * 1024;        // 10MB
    static const int COMPRESSION_THRESHOLD = 1024 * 1024;      // 1MB
    
    // 压缩缓冲区大小常量
    static const int COMPRESSION_BUFFER_SIZE = 8192;
    static const int DECOMPRESSION_BUFFER_SIZE = 8192;
    static const int STREAM_BUFFER_SIZE = 4096;
    static const int IMAGE_BUFFER_SIZE = 16384;
    
    // 窗口尺寸常量
    static const int MIN_WINDOW_WIDTH = 800;
    static const int MIN_WINDOW_HEIGHT = 600;
    static const int DEFAULT_WINDOW_WIDTH = 1024;
    static const int DEFAULT_WINDOW_HEIGHT = 768;
    static const int MAIN_WINDOW_WIDTH = 1200;
    static const int MAIN_WINDOW_HEIGHT = 800;
    static const int CONNECTION_DIALOG_WIDTH = 400;
    static const int CONNECTION_DIALOG_HEIGHT = 300;
    
    // 显示分辨率常量
    static const int MAX_DISPLAY_WIDTH = 1920;
    static const int MAX_DISPLAY_HEIGHT = 1080;
    
    // 历史记录常量
    static const int MAX_CONNECTION_HISTORY = 20;
    
    // 会话管理常量
    static const int STATS_UPDATE_INTERVAL = 500; // 500毫秒 - 提高统计更新频率
    static const int MAX_FRAME_HISTORY = 60; // 保留60帧的时间记录
    
    // 屏幕捕获常量
    static const int DEFAULT_FRAME_RATE = 60; // 默认60 FPS
    static const int MIN_FRAME_RATE = 1;
    static const int MAX_FRAME_RATE = 120;
    static const int DEBUG_LOG_INTERVAL = 100; // 每100帧输出一次调试信息
    static const int FAILURE_LOG_INTERVAL = 10; // 每10次失败输出一次调试信息
    static const int MILLISECONDS_PER_SECOND = 1000;
    
    // 捕获质量常量
    static constexpr double DEFAULT_CAPTURE_QUALITY = 0.9; // 默认高质量
    
    // 输入处理常量
    static const int DEFAULT_INPUT_BUFFER_SIZE = 100;
    static const int DEFAULT_INPUT_FLUSH_INTERVAL = 10; // 毫秒
    static const int MAX_PROCESSING_TIMES_HISTORY = 1000; // 保留1000次处理时间记录
    
    // 压缩算法参数常量
    static const int DEFAULT_ZLIB_LEVEL = 6;
    static const int DEFAULT_ZLIB_WINDOW_BITS = 15;
    static const int DEFAULT_ZLIB_MEM_LEVEL = 8;
    static const int MIN_WINDOW_BITS = 8;
    static const int MAX_WINDOW_BITS = 15;
    static const int SMALL_DATA_THRESHOLD = 1024; // 1KB
    
    // 输入模拟器常量
    static const int DEFAULT_MOUSE_SPEED = 5;
    static const int DEFAULT_KEYBOARD_DELAY = 10; // 毫秒
    static const int DEFAULT_MOUSE_DELAY = 10; // 毫秒
    static const int MAX_KEY_VALUE = 0xFFFF; // 最大键值
    
    // 服务器常量
    static const int DEFAULT_MAX_CLIENTS = 1;
    static const int CLEANUP_TIMER_INTERVAL = 1000; // 1秒，清理断开连接的客户端
    
    // 颜色常量
    static const QColor LIGHT_GRAY_COLOR;
    static const QColor WHITE_COLOR;
    static const QColor MEDIUM_GRAY_COLOR;
    
    // 动画常量
    static const double PULSE_MULTIPLIER;
    static const double PULSE_DIVISOR;
    static const int PULSE_BASE_ALPHA;
    static const int PULSE_AMPLITUDE;
    
private:
    UIConstants() = delete; // 禁止实例化
};

// 内联定义颜色常量
inline const QColor UIConstants::LIGHT_GRAY_COLOR(200, 200, 200);
inline const QColor UIConstants::WHITE_COLOR(255, 255, 255);
inline const QColor UIConstants::MEDIUM_GRAY_COLOR(150, 150, 150);

inline const double UIConstants::PULSE_MULTIPLIER = 100.0;
inline const double UIConstants::PULSE_DIVISOR = 30.0;
inline const int UIConstants::PULSE_BASE_ALPHA = 100;
inline const int UIConstants::PULSE_AMPLITUDE = 100;

// 内联定义整型常量
inline const int UIConstants::DEFAULT_FRAME_RATE;
inline const int UIConstants::MIN_FRAME_RATE;
inline const int UIConstants::MAX_FRAME_RATE;
inline const int UIConstants::DEBUG_LOG_INTERVAL;
inline const int UIConstants::FAILURE_LOG_INTERVAL;
inline const int UIConstants::MILLISECONDS_PER_SECOND;
inline const int UIConstants::DEFAULT_INPUT_BUFFER_SIZE;
inline const int UIConstants::DEFAULT_INPUT_FLUSH_INTERVAL;
inline const int UIConstants::MAX_PROCESSING_TIMES_HISTORY;
inline const int UIConstants::DEFAULT_ZLIB_LEVEL;
inline const int UIConstants::DEFAULT_ZLIB_WINDOW_BITS;
inline const int UIConstants::DEFAULT_ZLIB_MEM_LEVEL;
inline const int UIConstants::MIN_WINDOW_BITS;
inline const int UIConstants::MAX_WINDOW_BITS;
inline const int UIConstants::SMALL_DATA_THRESHOLD;
inline const int UIConstants::DEFAULT_MOUSE_SPEED;
inline const int UIConstants::DEFAULT_KEYBOARD_DELAY;
inline const int UIConstants::DEFAULT_MOUSE_DELAY;
inline const int UIConstants::MAX_KEY_VALUE;
inline const int UIConstants::DEFAULT_MAX_CLIENTS;
inline const int UIConstants::CLEANUP_TIMER_INTERVAL;

// 内联定义浮点常量
inline constexpr double UIConstants::DEFAULT_CAPTURE_QUALITY;

#endif // UICONSTANTS_H