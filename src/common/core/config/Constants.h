#ifndef CORE_CONSTANTS_H
#define CORE_CONSTANTS_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QLoggingCategory>
#include <memory>

/**
 * @brief 核心常量定义类
 *
 * 提供系统级别的常量定义，按功能模块分组管理。
 * 使用命名空间和结构体提供更好的类型安全性和组织结构。
 *
 */
class CoreConstants : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 系统版本信息
     */
    struct Version {
        static constexpr int MAJOR = 1;
        static constexpr int MINOR = 0;
        static constexpr int PATCH = 0;
        static const QString VERSION_STRING;
        static const QString BUILD_DATE;
    };

    /**
     * @brief 文件和帧大小相关常量
     */
    struct Frame {
        static constexpr int DEFAULT_MAX_FILE_SIZE = 10 * 1024 * 1024;  ///< 默认最大文件大小 10MB
        static constexpr int MAX_FRAME_SIZE = 10 * 1024 * 1024;         ///< 最大帧大小 10MB
        static constexpr int MIN_FRAME_SIZE = 1024;                     ///< 最小帧大小 1KB
        static constexpr int FRAME_HEADER_SIZE = 32;                    ///< 帧头大小 32字节
    };

    /**
     * @brief 缓冲区相关常量
     */
    struct Buffer {
        static constexpr int STREAM_BUFFER_SIZE = 32 * 1024;            ///< 流缓冲区大小 32KB
        static constexpr int IMAGE_BUFFER_SIZE = 512 * 1024;            ///< 图像缓冲区大小 512KB
    };

    /**
     * @brief 捕获和帧率相关常量
     */
    struct Capture {
        static constexpr int DEFAULT_FRAME_RATE = 30;                   ///< 默认帧率 30fps
        static constexpr int MIN_FRAME_RATE = 1;                        ///< 最小帧率 1fps
        static constexpr int MAX_FRAME_RATE = 120;                      ///< 最大帧率 120fps
        static constexpr int DEBUG_LOG_INTERVAL = 1000;                 ///< 调试日志间隔 1000ms
        static constexpr int FAILURE_LOG_INTERVAL = 5000;               ///< 失败日志间隔 5000ms
        static constexpr int MILLISECONDS_PER_SECOND = 1000;            ///< 每秒毫秒数
    };

    /**
     * @brief 图像压缩相关常量
     */
    struct Compression {
        static constexpr int JPEG_QUALITY_HIGH = 85;                    ///< 高质量 JPEG 压缩
        static constexpr int JPEG_QUALITY_MEDIUM = 70;                  ///< 中等质量 JPEG 压缩
        static constexpr int JPEG_QUALITY_LOW = 50;                     ///< 低质量 JPEG 压缩
        static constexpr int JPEG_QUALITY_MIN = 30;                     ///< 最低质量 JPEG 压缩
        static constexpr int DEFAULT_JPEG_QUALITY = 85;                 ///< 默认 JPEG 质量（平衡大小与质量）
        static constexpr bool ENABLE_ZSTD_COMPRESSION = true;           ///< 启用 zstd 二次压缩
        static constexpr int ZSTD_COMPRESSION_LEVEL = 2;                ///< zstd 压缩级别 (1=快速, 2=最佳压缩, 推荐3-5)
        static constexpr int MIN_SIZE_FOR_ZSTD = 1024;                  ///< 启用 zstd 的最小数据大小 (字节)
        static constexpr double SCALE_FACTOR_HIGH = 1.0;                ///< 高清缩放因子
        static constexpr double SCALE_FACTOR_MEDIUM = 0.75;             ///< 中等缩放因子
        static constexpr double SCALE_FACTOR_LOW = 0.5;                 ///< 低清缩放因子
        static constexpr bool ENABLE_ADAPTIVE_QUALITY = true;           ///< 启用自适应质量调整
        static constexpr int QUEUE_HIGH_WATERMARK = 60;                 ///< 队列高水位（触发质量降低）
        static constexpr int QUEUE_LOW_WATERMARK = 20;                  ///< 队列低水位（恢复质量）
    };

    /**
     * @brief 输入处理相关常量
     */
    struct Input {
        static constexpr int DEFAULT_MOUSE_SPEED = 5;                   ///< 默认鼠标速度 px/step
        static constexpr int DEFAULT_KEYBOARD_DELAY = 50;               ///< 默认键盘延迟 ms
        static constexpr int DEFAULT_MOUSE_DELAY = 10;                  ///< 默认鼠标延迟 ms
        static constexpr int MAX_KEY_VALUE = 0x01FFFFFF;                ///< 最大按键值 (Qt::Key 最大值)
    };

    /**
     * @brief 性能相关常量
     */
    struct Performance {
        static constexpr int THREAD_POOL_SIZE = 4;                      ///< 线程池大小
        static constexpr int MAX_QUEUE_SIZE = 1000;                     ///< 最大队列大小
        static constexpr int STATS_UPDATE_INTERVAL_MS = 1000;           ///< 统计更新间隔 1s
        static constexpr int MEMORY_WARNING_THRESHOLD_MB = 512;          ///< 内存警告阈值 512MB
        static constexpr int CPU_USAGE_THRESHOLD_PERCENT = 80;          ///< CPU使用率阈值 80%
        static constexpr int GC_INTERVAL_MS = 30000;                    ///< 垃圾回收间隔 30s
    };

    /**
     * @brief 安全相关常量
     */
    struct Security {
        static constexpr int AES_KEY_SIZE = 256;                        ///< AES密钥大小 256位
        static constexpr int RSA_KEY_SIZE = 2048;                       ///< RSA密钥大小 2048位
        static constexpr int SALT_SIZE = 16;                            ///< 盐值大小 16字节
        static constexpr int HASH_ITERATIONS = 10000;                   ///< 哈希迭代次数
        static constexpr int SESSION_TIMEOUT_MS = 3600000;              ///< 会话超时时间 1小时
        static const QString DEFAULT_CIPHER_SUITE;                      ///< 默认加密套件
    };

    // 静态工具方法
    /**
     * @brief 获取应用程序版本信息
     * @return 版本字符串
     */
    static QString getVersionString();

    /**
     * @brief 获取构建日期
     * @return 构建日期字符串
     */
    static QString getBuildDate();

    /**
     * @brief 验证帧率是否在有效范围内
     * @param fps 帧率值
     * @return 是否有效
     */
    static bool isValidFrameRate(int fps);

    /**
     * @brief 验证端口号是否有效
     * @param port 端口号
     * @return 是否有效
     */
    static bool isValidPort(int port);

    /**
     * @brief 获取推荐的线程池大小
     * @return 线程池大小
     */
    static int getRecommendedThreadPoolSize();

private:
    CoreConstants() = delete;
};

#endif // CORE_CONSTANTS_H
