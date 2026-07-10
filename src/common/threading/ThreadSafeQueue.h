#pragma once

#include <QtCore/QMutex>
#include <QtCore/QQueue>
#include <QtCore/QMutexLocker>
#include <memory>
#include <chrono>

/**
 * @brief 线程安全队列模板类（纯非阻塞）
 *
 * 提供线程安全的非阻塞入队/出队操作，适用于生产者-消费者模式。
 * 不提供阻塞等待——调用方负责轮询或上层循环。
 *
 * @tparam T 队列元素类型
 */
template<typename T>
class ThreadSafeQueue
{
public:
    /**
     * @brief 构造函数
     * @param maxSize 队列最大容量，0表示无限制
     */
    explicit ThreadSafeQueue(int maxSize = 0)
        : m_maxSize(maxSize)
        , m_totalEnqueued(0)
        , m_totalDequeued(0)
        , m_totalDropped(0)
    {
    }

    /**
     * @brief 禁用拷贝构造和赋值操作
     */
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    /**
     * @brief 非阻塞入队
     *
     * 如果队列已满，立即返回 false 而不阻塞。
     *
     * @param item 要入队的元素
     * @return true 成功入队，false 队列已满
     */
    bool tryEnqueue(const T& item)
    {
        QMutexLocker locker(&m_mutex);

        if (m_maxSize > 0 && m_queue.size() >= m_maxSize) {
            return false;
        }

        m_queue.enqueue(item);
        ++m_totalEnqueued;
        return true;
    }

    /**
     * @brief 入队——满时清空队列后入队（保证始终保留最新数据）
     *
     * 当队列已满时，清空队列中的所有旧数据，
     * 然后将新数据入队。适合实时流场景（如视频帧管线），
     * 积压的旧帧应被丢弃以维持低延迟。
     *
     * @param item 要入队的元素
     * @return 被丢弃的帧数（0 = 未丢弃，>0 = 清空了若干旧帧）
     */
    int tryEnqueueDrainToLatest(const T& item)
    {
        QMutexLocker locker(&m_mutex);

        int dropped = 0;
        if (m_maxSize > 0 && m_queue.size() >= m_maxSize) {
            dropped = m_queue.size();
            m_queue.clear();
            m_totalDropped += dropped;
        }

        m_queue.enqueue(item);
        ++m_totalEnqueued;
        return dropped;
    }

    /**
     * @brief 非阻塞出队
     *
     * 如果队列为空，立即返回 false 而不阻塞。
     *
     * @param item 输出参数，存储出队的元素
     * @return true 成功出队，false 队列为空
     */
    bool tryDequeue(T& item)
    {
        QMutexLocker locker(&m_mutex);

        if (m_queue.isEmpty()) {
            return false;
        }

        item = m_queue.dequeue();
        ++m_totalDequeued;
        return true;
    }

    /**
     * @brief 获取队列当前大小
     * @return 队列中元素的数量
     */
    int size() const
    {
        QMutexLocker locker(&m_mutex);
        return m_queue.size();
    }

    /**
     * @brief 检查队列是否为空
     * @return true 队列为空，false 队列不为空
     */
    bool isEmpty() const
    {
        QMutexLocker locker(&m_mutex);
        return m_queue.isEmpty();
    }

    /**
     * @brief 检查队列是否已满
     * @return true 队列已满，false 队列未满或无大小限制
     */
    bool isFull() const
    {
        QMutexLocker locker(&m_mutex);
        return m_maxSize > 0 && m_queue.size() >= m_maxSize;
    }

    /**
     * @brief 清空队列
     */
    void clear()
    {
        QMutexLocker locker(&m_mutex);
        m_queue.clear();
    }

    /**
     * @brief 获取队列最大容量
     * @return 最大容量，0表示无限制
     */
    int maxSize() const
    {
        return m_maxSize;
    }

    /**
     * @brief 设置队列最大容量
     * @param maxSize 最大容量，0表示无限制
     */
    void setMaxSize(int maxSize)
    {
        QMutexLocker locker(&m_mutex);
        m_maxSize = maxSize;
    }

    /**
     * @brief 获取总入队数量
     * @return 总入队数量
     */
    quint64 getTotalEnqueued() const
    {
        QMutexLocker locker(&m_mutex);
        return m_totalEnqueued;
    }

    /**
     * @brief 获取总出队数量
     * @return 总出队数量
     */
    quint64 getTotalDequeued() const
    {
        QMutexLocker locker(&m_mutex);
        return m_totalDequeued;
    }

    /**
     * @brief 获取因 drain 清空而丢弃的总数
     * @return 丢弃总数
     */
    quint64 getTotalDropped() const
    {
        QMutexLocker locker(&m_mutex);
        return m_totalDropped;
    }

private:
    mutable QMutex m_mutex;           ///< 互斥锁
    QQueue<T> m_queue;                ///< 底层队列
    int m_maxSize;                    ///< 最大容量，0表示无限制
    quint64 m_totalEnqueued;          ///< 总入队数量
    quint64 m_totalDequeued;          ///< 总出队数量
    quint64 m_totalDropped;           ///< 因 drain 清空而丢弃的总数
};
