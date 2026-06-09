#pragma once

#include <QtCore/QObject>

/**
 * @brief 核心常量定义类
 *
 * 提供系统级别的常量定义，按功能模块分组管理。
 */
class CoreConstants : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 捕获和帧率相关常量
     */
    struct Capture {
        static constexpr int DEFAULT_FRAME_RATE = 120;                  ///< 默认帧率 120fps
        static constexpr int MIN_FRAME_RATE = 1;                        ///< 最小帧率 1fps
        static constexpr int MAX_FRAME_RATE = 120;                      ///< 最大帧率 120fps
        static constexpr int CAPTURE_QUEUE_SIZE = 120;                  ///< 捕获队列容量 120帧（流水池模型）
        static constexpr int PROCESSED_QUEUE_SIZE = 120;                ///< 处理队列容量 120帧（流水池模型）
    };

    /**
     * @brief 图像压缩相关常量
     */
    struct Compression {
#ifdef QT_DEBUG
        static constexpr int DEFAULT_WEBP_QUALITY = 30;       ///< WebP 质量（Debug 构建低质量，保证编码速度）
#else
        static constexpr int DEFAULT_WEBP_QUALITY = 95;       ///< WebP 质量（Release 构建近无损）
#endif
        static constexpr double SCALE_FACTOR_HIGH = 1.0;      ///< 高清缩放因子
    };

    /**
     * @brief 客户端相关常量
     */
    struct Client {
        static constexpr int DECODE_QUEUE_SIZE = 4;                     ///< 客户端解码队列容量（流水池模型）
    };

private:
    CoreConstants() = delete;
};
