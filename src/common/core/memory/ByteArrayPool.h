#ifndef BYTEARRAYPOOL_H
#define BYTEARRAYPOOL_H

#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QMutex>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QQueue>
#include <QtCore/QHash>
#include <memory>

/**
 * @brief QByteArray对象池类
 * 
 * 高效管理QByteArray对象的内存池，用于网络传输和数据处理场景。
 * 特性：
 * - 按容量大小分类管理
 * - 自动扩容和收缩
 * - 过期对象自动清理
 * - 内存使用统计
 * - 线程安全
 * - 智能预分配
 */
class ByteArrayPool : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 缓冲池统计信息
     */
    struct PoolStats {
        int totalBuffers = 0;           ///< 总缓冲区数量
        int availableBuffers = 0;       ///< 可用缓冲区数量
        int usedBuffers = 0;            ///< 已使用缓冲区数量
        qint64 totalMemory = 0;         ///< 总内存使用量（字节）
        qint64 availableMemory = 0;     ///< 可用内存量（字节）
        qint64 hitCount = 0;            ///< 命中次数
        qint64 missCount = 0;           ///< 未命中次数
        double hitRate = 0.0;           ///< 命中率
        int poolCount = 0;              ///< 池数量
    };

    /**
     * @brief 缓冲区项目
     */
    struct BufferPoolItem {
        std::shared_ptr<QByteArray> buffer; ///< 缓冲区对象
        QDateTime lastUsed;                 ///< 最后使用时间
        int capacity = 0;                   ///< 缓冲区容量
        bool inUse = false;                 ///< 是否正在使用
    };

    /**
     * @brief 容量池
     */
    struct CapacityPool {
        QQueue<std::shared_ptr<BufferPoolItem>> availableItems; ///< 可用项目队列
        QHash<QByteArray*, std::shared_ptr<BufferPoolItem>> usedItems; ///< 已使用项目映射
        int capacity = 0;                   ///< 池容量大小
        qint64 hitCount = 0;                ///< 命中次数
        qint64 missCount = 0;               ///< 未命中次数
    };

public:
    // 默认配置常量
    static constexpr int DEFAULT_MAX_POOL_SIZE = 50;        ///< 默认最大池大小
    static constexpr int DEFAULT_CLEANUP_INTERVAL = 30000;  ///< 默认清理间隔（30秒）
    static constexpr int DEFAULT_OBJECT_TIMEOUT = 300000;   ///< 默认对象过期时间（5分钟）
    static constexpr int DEFAULT_MIN_CAPACITY = 1024;       ///< 默认最小容量（1KB）
    static constexpr int DEFAULT_MAX_CAPACITY = 10485760;   ///< 默认最大容量（10MB）

    /**
     * @brief 获取单例实例
     * @return ByteArrayPool实例指针
     */
    static ByteArrayPool* instance();

    /**
     * @brief 析构函数
     */
    ~ByteArrayPool();

    /**
     * @brief 获取指定容量的缓冲区
     * @param capacity 所需容量（字节）
     * @return 缓冲区智能指针
     */
    std::shared_ptr<QByteArray> acquireBuffer(int capacity);

    /**
     * @brief 归还缓冲区到池中
     * @param buffer 要归还的缓冲区
     */
    void releaseBuffer(std::shared_ptr<QByteArray> buffer);

    /**
     * @brief 预分配指定容量的缓冲区
     * @param capacity 缓冲区容量
     * @param count 预分配数量
     */
    void preallocateBuffers(int capacity, int count);

    /**
     * @brief 清理指定容量的池
     * @param capacity 要清理的容量大小
     */
    void clearPool(int capacity);

    /**
     * @brief 清理所有池
     */
    void clearAllPools();

    /**
     * @brief 设置最大池大小
     * @param maxSize 最大池大小
     */
    void setMaxPoolSize(int maxSize);

    /**
     * @brief 获取最大池大小
     * @return 最大池大小
     */
    int maxPoolSize() const;

    /**
     * @brief 设置清理间隔
     * @param interval 清理间隔（毫秒）
     */
    void setCleanupInterval(int interval);

    /**
     * @brief 获取清理间隔
     * @return 清理间隔（毫秒）
     */
    int cleanupInterval() const;

    /**
     * @brief 设置对象过期时间
     * @param timeout 过期时间（毫秒）
     */
    void setObjectTimeout(int timeout);

    /**
     * @brief 获取对象过期时间
     * @return 过期时间（毫秒）
     */
    int objectTimeout() const;

    /**
     * @brief 设置容量范围
     * @param minCapacity 最小容量
     * @param maxCapacity 最大容量
     */
    void setCapacityRange(int minCapacity, int maxCapacity);

    /**
     * @brief 获取最小容量
     * @return 最小容量
     */
    int minCapacity() const;

    /**
     * @brief 获取最大容量
     * @return 最大容量
     */
    int maxCapacity() const;

    /**
     * @brief 获取全局池统计信息
     * @return 统计信息结构
     */
    PoolStats getPoolStats() const;

    /**
     * @brief 获取指定容量的池统计信息
     * @param capacity 容量大小
     * @return 统计信息结构
     */
    PoolStats getPoolStats(int capacity) const;

    /**
     * @brief 启用或禁用池功能
     * @param enabled 是否启用
     */
    void setEnabled(bool enabled);

    /**
     * @brief 检查池功能是否启用
     * @return 是否启用
     */
    bool isEnabled() const;

    /**
     * @brief 获取推荐的容量大小（向上取整到2的幂）
     * @param requestedCapacity 请求的容量
     * @return 推荐的容量
     */
    int getRecommendedCapacity(int requestedCapacity) const;

signals:
    /**
     * @brief 池统计信息更新信号
     * @param stats 统计信息
     */
    void poolStatsUpdated(const PoolStats &stats);

    /**
     * @brief 内存使用量变化信号
     * @param totalMemory 总内存使用量
     * @param availableMemory 可用内存量
     */
    void memoryUsageChanged(qint64 totalMemory, qint64 availableMemory);

    /**
     * @brief 池容量变化信号
     * @param capacity 容量大小
     * @param poolSize 池大小
     */
    void poolSizeChanged(int capacity, int poolSize);

private slots:
    /**
     * @brief 清理过期对象
     */
    void cleanupExpiredObjects();

private:
    /**
     * @brief 私有构造函数（单例模式）
     * @param parent 父对象
     */
    explicit ByteArrayPool(QObject *parent = nullptr);

    /**
     * @brief 创建缓冲区项目
     * @param capacity 缓冲区容量
     * @return 缓冲区项目智能指针
     */
    std::shared_ptr<BufferPoolItem> createBufferItem(int capacity);

    /**
     * @brief 获取容量键值
     * @param capacity 容量大小
     * @return 键值字符串
     */
    QString getCapacityKey(int capacity) const;

    /**
     * @brief 更新统计信息
     */
    void updateStats();

    /**
     * @brief 验证容量是否在有效范围内
     * @param capacity 容量大小
     * @return 是否有效
     */
    bool isValidCapacity(int capacity) const;

    // 静态成员
    static ByteArrayPool* s_instance;   ///< 单例实例
    static QMutex s_instanceMutex;      ///< 实例互斥锁

    // 成员变量
    mutable QMutex m_mutex;                                 ///< 线程安全互斥锁
    QHash<QString, CapacityPool> m_pools;                   ///< 容量池映射
    QTimer* m_cleanupTimer;                                 ///< 清理定时器
    PoolStats m_globalStats;                               ///< 全局统计信息
    
    // 配置参数
    int m_maxPoolSize;          ///< 最大池大小
    int m_cleanupInterval;      ///< 清理间隔
    int m_objectTimeout;        ///< 对象过期时间
    int m_minCapacity;          ///< 最小容量
    int m_maxCapacity;          ///< 最大容量
    bool m_enabled;             ///< 是否启用池功能
};

#endif // BYTEARRAYPOOL_H