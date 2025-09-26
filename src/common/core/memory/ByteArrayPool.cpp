#include "ByteArrayPool.h"
// Logger已迁移到logging_categories
#include "../logging/LoggingCategories.h"
#include <QtCore/QMutexLocker>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <cmath>

// 静态成员初始化
ByteArrayPool* ByteArrayPool::s_instance = nullptr;
QMutex ByteArrayPool::s_instanceMutex;

ByteArrayPool* ByteArrayPool::instance()
{
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        s_instance = new ByteArrayPool();
    }
    return s_instance;
}

ByteArrayPool::ByteArrayPool(QObject *parent)
    : QObject(parent)
    , m_maxPoolSize(DEFAULT_MAX_POOL_SIZE)
    , m_cleanupInterval(DEFAULT_CLEANUP_INTERVAL)
    , m_objectTimeout(DEFAULT_OBJECT_TIMEOUT)
    , m_minCapacity(DEFAULT_MIN_CAPACITY)
    , m_maxCapacity(DEFAULT_MAX_CAPACITY)
    , m_enabled(true)
{
    qCInfo(lcPerformance, "字节数组对象池初始化");
    
    // 初始化统计信息
    m_globalStats = {};
    
    // 创建清理定时器
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &ByteArrayPool::cleanupExpiredObjects);
    m_cleanupTimer->start(m_cleanupInterval);
    
    qCDebug(lcPerformance, "字节数组池配置 - 最大池大小: %d, 清理间隔: %dms, 对象过期时间: %dms, 容量范围: %d-%d",
              m_maxPoolSize, m_cleanupInterval, m_objectTimeout, m_minCapacity, m_maxCapacity);
}

ByteArrayPool::~ByteArrayPool()
{
    qCInfo(lcPerformance, "字节数组对象池销毁");
    clearAllPools();
}

std::shared_ptr<QByteArray> ByteArrayPool::acquireBuffer(int capacity)
{
    if (!m_enabled || !isValidCapacity(capacity)) {
        // 池功能禁用或容量无效时，直接创建新对象
        auto buffer = std::make_shared<QByteArray>();
        buffer->reserve(capacity);
        return buffer;
    }
    
    // 获取推荐容量（向上取整到2的幂）
    int recommendedCapacity = getRecommendedCapacity(capacity);
    
    QMutexLocker locker(&m_mutex);
    
    QString capacityKey = getCapacityKey(recommendedCapacity);
    
    // 检查是否存在对应容量的池
    if (!m_pools.contains(capacityKey)) {
        m_pools[capacityKey] = CapacityPool();
        m_pools[capacityKey].capacity = recommendedCapacity;
        m_pools[capacityKey].hitCount = 0;
        m_pools[capacityKey].missCount = 0;
    }
    
    CapacityPool &pool = m_pools[capacityKey];
    
    // 尝试从池中获取可用对象
    if (!pool.availableItems.isEmpty()) {
        auto item = pool.availableItems.dequeue();
        item->inUse = true;
        item->lastUsed = QDateTime::currentDateTime();
        
        // 清空缓冲区内容并重新设置容量
        item->buffer->clear();
        item->buffer->reserve(capacity);
        
        // 将对象移到已使用映射中
        pool.usedItems[item->buffer.get()] = item;
        pool.hitCount++;

        updateStats();
        return item->buffer;
    }
    
    // 池中没有可用对象，创建新对象
    auto item = createBufferItem(recommendedCapacity);
    item->inUse = true;
    item->lastUsed = QDateTime::currentDateTime();
    
    // 设置实际需要的容量
    item->buffer->reserve(capacity);
    
    // 添加到已使用映射中
    pool.usedItems[item->buffer.get()] = item;
    pool.missCount++;

    updateStats();
    return item->buffer;
}

void ByteArrayPool::releaseBuffer(std::shared_ptr<QByteArray> buffer)
{
    if (!buffer) {
        return;
    }
    
    // 如果池功能禁用，直接返回（缓冲区会自动销毁）
    if (!m_enabled) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    QByteArray* rawPtr = buffer.get();
    
    // 查找缓冲区所属的池
    CapacityPool* targetPool = nullptr;
    std::shared_ptr<BufferPoolItem> item;
    
    for (auto &pool : m_pools) {
        if (pool.usedItems.contains(rawPtr)) {
            targetPool = &pool;
            item = pool.usedItems.take(rawPtr);
            break;
        }
    }
    
    if (!targetPool || !item) {
        // 这可能是一个直接创建的缓冲区（容量无效或池功能当时禁用）
        return;
    }
    
    item->inUse = false;
    item->lastUsed = QDateTime::currentDateTime();
    
    // 检查池大小限制
    if (targetPool->availableItems.size() < m_maxPoolSize) {
        // 清空缓冲区内容以节省内存
        item->buffer->clear();
        item->buffer->squeeze(); // 释放多余内存
        
        // 归还到可用队列
        targetPool->availableItems.enqueue(item);

    } else {
        // 池已满时，缓冲区将被丢弃，但需要确保统计信息正确
        // 注意：item会在函数结束时自动销毁，不需要手动处理
    }
    
    updateStats();
}

void ByteArrayPool::preallocateBuffers(int capacity, int count)
{
    if (!m_enabled || count <= 0 || !isValidCapacity(capacity)) {
        return;
    }
    
    // 获取推荐容量
    int recommendedCapacity = getRecommendedCapacity(capacity);
    
    QMutexLocker locker(&m_mutex);
    
    QString capacityKey = getCapacityKey(recommendedCapacity);
    
    // 确保池存在
    if (!m_pools.contains(capacityKey)) {
        m_pools[capacityKey] = CapacityPool();
        m_pools[capacityKey].capacity = recommendedCapacity;
        m_pools[capacityKey].hitCount = 0;
        m_pools[capacityKey].missCount = 0;
    }
    
    CapacityPool &pool = m_pools[capacityKey];
    
    // 预分配指定数量的缓冲区
    int allocated = 0;
    for (int i = 0; i < count && pool.availableItems.size() < m_maxPoolSize; ++i) {
        auto item = createBufferItem(recommendedCapacity);
        pool.availableItems.enqueue(item);
        allocated++;
    }
    
    qCInfo(lcPerformance, "预分配缓冲区完成: %d字节(推荐%d), 数量: %d, 池中总数: %lld",
             capacity, recommendedCapacity,
             allocated,
             static_cast<long long>(pool.availableItems.size()));
    
    updateStats();
    emit poolSizeChanged(recommendedCapacity, pool.availableItems.size());
}

void ByteArrayPool::clearPool(int capacity)
{
    QMutexLocker locker(&m_mutex);
    
    int recommendedCapacity = getRecommendedCapacity(capacity);
    QString capacityKey = getCapacityKey(recommendedCapacity);
    
    if (m_pools.contains(capacityKey)) {
        qCDebug(lcPerformance, "清理缓冲区池: %d字节", recommendedCapacity);
        m_pools.remove(capacityKey);
        updateStats();
        emit poolSizeChanged(recommendedCapacity, 0);
    }
}

void ByteArrayPool::clearAllPools()
{
    QMutexLocker locker(&m_mutex);
    
    qCInfo(lcPerformance, "清理所有缓冲区池，总池数: %lld", static_cast<long long>(m_pools.size()));
    m_pools.clear();
    
    // 重置统计信息
    m_globalStats = {};
    
    updateStats();
}

void ByteArrayPool::setMaxPoolSize(int maxSize)
{
    QMutexLocker locker(&m_mutex);
    m_maxPoolSize = qMax(1, maxSize);
    qCDebug(lcPerformance, "设置最大池大小: %d", m_maxPoolSize);
}

int ByteArrayPool::maxPoolSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxPoolSize;
}

void ByteArrayPool::setCleanupInterval(int interval)
{
    QMutexLocker locker(&m_mutex);
    m_cleanupInterval = qMax(1000, interval); // 最小1秒
    m_cleanupTimer->setInterval(m_cleanupInterval);
    qCDebug(lcPerformance, "设置清理间隔: %dms", m_cleanupInterval);
}

int ByteArrayPool::cleanupInterval() const
{
    QMutexLocker locker(&m_mutex);
    return m_cleanupInterval;
}

void ByteArrayPool::setObjectTimeout(int timeout)
{
    QMutexLocker locker(&m_mutex);
    m_objectTimeout = qMax(1000, timeout); // 最小1秒
    qCDebug(lcPerformance, "设置对象过期时间: %dms", m_objectTimeout);
}

int ByteArrayPool::objectTimeout() const
{
    QMutexLocker locker(&m_mutex);
    return m_objectTimeout;
}

void ByteArrayPool::setCapacityRange(int minCapacity, int maxCapacity)
{
    QMutexLocker locker(&m_mutex);
    m_minCapacity = qMax(1, minCapacity);
    m_maxCapacity = qMax(m_minCapacity, maxCapacity);
    qCDebug(lcPerformance, "设置容量范围: %d-%d字节", m_minCapacity, m_maxCapacity);
}

int ByteArrayPool::minCapacity() const
{
    QMutexLocker locker(&m_mutex);
    return m_minCapacity;
}

int ByteArrayPool::maxCapacity() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxCapacity;
}

ByteArrayPool::PoolStats ByteArrayPool::getPoolStats() const
{
    QMutexLocker locker(&m_mutex);
    return m_globalStats;
}

ByteArrayPool::PoolStats ByteArrayPool::getPoolStats(int capacity) const
{
    QMutexLocker locker(&m_mutex);
    
    int recommendedCapacity = getRecommendedCapacity(capacity);
    QString capacityKey = getCapacityKey(recommendedCapacity);
    
    PoolStats stats = {};
    
    if (m_pools.contains(capacityKey)) {
        const CapacityPool &pool = m_pools[capacityKey];
        stats.availableBuffers = pool.availableItems.size();
        stats.usedBuffers = pool.usedItems.size();
        stats.totalBuffers = stats.availableBuffers + stats.usedBuffers;
        stats.hitCount = pool.hitCount;
        stats.missCount = pool.missCount;
        stats.hitRate = (stats.hitCount + stats.missCount) > 0 ? 
                       static_cast<double>(stats.hitCount) / (stats.hitCount + stats.missCount) : 0.0;
        
        // 计算内存使用量
        stats.availableMemory = stats.availableBuffers * recommendedCapacity;
        stats.totalMemory = stats.totalBuffers * recommendedCapacity;
        stats.poolCount = 1;
    }
    
    return stats;
}

void ByteArrayPool::setEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_enabled = enabled;
    qCInfo(lcPerformance, "字节数组池功能%s", enabled ? "启用" : "禁用");
    
    if (!enabled) {
        // 禁用时清理所有池
        clearAllPools();
    }
}

bool ByteArrayPool::isEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_enabled;
}

int ByteArrayPool::getRecommendedCapacity(int requestedCapacity) const
{
    if (requestedCapacity <= 0) {
        return m_minCapacity;
    }
    
    // 确保在有效范围内
    int capacity = qBound(m_minCapacity, requestedCapacity, m_maxCapacity);
    
    // 向上取整到2的幂，以提高内存利用率
    int powerOfTwo = 1;
    while (powerOfTwo < capacity) {
        powerOfTwo <<= 1;
    }
    
    // 确保不超过最大容量
    return qMin(powerOfTwo, m_maxCapacity);
}

void ByteArrayPool::cleanupExpiredObjects()
{
    if (!m_enabled) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    QDateTime now = QDateTime::currentDateTime();
    int cleanedCount = 0;
    
    // 遍历所有池
    for (auto poolIt = m_pools.begin(); poolIt != m_pools.end(); ++poolIt) {
        CapacityPool &pool = poolIt.value();
        
        // 清理可用队列中的过期对象
        auto &availableItems = pool.availableItems;
        for (int i = availableItems.size() - 1; i >= 0; --i) {
            auto item = availableItems[i];
            if (item->lastUsed.msecsTo(now) > m_objectTimeout) {
                availableItems.removeAt(i);
                cleanedCount++;
            }
        }
        
        // 如果池为空，发射信号
        if (pool.availableItems.isEmpty() && pool.usedItems.isEmpty()) {
            emit poolSizeChanged(pool.capacity, 0);
        }
    }
    
    if (cleanedCount > 0) {
        qCDebug(lcPerformance, "清理过期缓冲区对象: %d个", cleanedCount);
        updateStats();
    }
}

std::shared_ptr<ByteArrayPool::BufferPoolItem> ByteArrayPool::createBufferItem(int capacity)
{
    auto item = std::make_shared<BufferPoolItem>();
    item->buffer = std::make_shared<QByteArray>();
    item->buffer->reserve(capacity);
    item->lastUsed = QDateTime::currentDateTime();
    item->capacity = capacity;
    item->inUse = false;
    
    return item;
}

QString ByteArrayPool::getCapacityKey(int capacity) const
{
    return QString("cap_%1").arg(capacity);
}

void ByteArrayPool::updateStats()
{
    // 重新计算全局统计信息
    m_globalStats = {};
    
    for (auto it = m_pools.begin(); it != m_pools.end(); ++it) {
        const CapacityPool &pool = it.value();
        
        m_globalStats.availableBuffers += pool.availableItems.size();
        m_globalStats.usedBuffers += pool.usedItems.size();
        m_globalStats.hitCount += pool.hitCount;
        m_globalStats.missCount += pool.missCount;
        m_globalStats.poolCount++;
        
        // 计算内存使用量
        m_globalStats.availableMemory += pool.availableItems.size() * pool.capacity;
        m_globalStats.totalMemory += (pool.availableItems.size() + pool.usedItems.size()) * pool.capacity;
    }
    
    m_globalStats.totalBuffers = m_globalStats.availableBuffers + m_globalStats.usedBuffers;
    m_globalStats.hitRate = (m_globalStats.hitCount + m_globalStats.missCount) > 0 ? 
                           static_cast<double>(m_globalStats.hitCount) / (m_globalStats.hitCount + m_globalStats.missCount) : 0.0;
    
    // 发射统计信息更新信号
    emit poolStatsUpdated(m_globalStats);
    emit memoryUsageChanged(m_globalStats.totalMemory, m_globalStats.availableMemory);
}

bool ByteArrayPool::isValidCapacity(int capacity) const
{
    return capacity > 0 && capacity >= m_minCapacity && capacity <= m_maxCapacity;
}