#ifndef THREADSAFEQUEUE_H
#define THREADSAFEQUEUE_H

#include <QtCore/QMutex>
#include <QtCore/QWaitCondition>
#include <QtCore/QQueue>
#include <QtCore/QMutexLocker>
#include <memory>
#include <chrono>

/**
 * @brief 线程安全队列模板类
 * 
 * 提供线程安全的队列操作，支持阻塞和非阻塞的入队出队操作。
 * 适用于生产者-消费者模式的多线程数据传递。
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
        , m_stopped(false)
        , m_totalEnqueued(0)
        , m_totalDequeued(0)
    {
    }

    /**
     * @brief 析构函数
     */
    ~ThreadSafeQueue()
    {
        stop();
    }

    /**
     * @brief 禁用拷贝构造和赋值操作
     */
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    /**
     * @brief 入队操作（阻塞版本）
     * 
     * 如果队列已满，会阻塞等待直到有空间或队列被停止。
     * 
     * @param item 要入队的元素
     * @return true 成功入队，false 队列已停止
     */
    bool enqueue(const T& item)
    {
        QMutexLocker locker(&m_mutex);
        
        // 等待队列有空间或被停止
        while (!m_stopped && m_maxSize > 0 && m_queue.size() >= m_maxSize) {
            m_notFull.wait(&m_mutex);
        }
        
        if (m_stopped) {
            return false;
        }
        
        m_queue.enqueue(item);
        ++m_totalEnqueued;
        m_notEmpty.wakeOne();
        return true;
    }

    /**
     * @brief 入队操作（移动语义版本）
     */
    bool enqueue(T&& item)
    {
        QMutexLocker locker(&m_mutex);
        
        while (!m_stopped && m_maxSize > 0 && m_queue.size() >= m_maxSize) {
            m_notFull.wait(&m_mutex);
        }
        
        if (m_stopped) {
            return false;
        }
        
        m_queue.enqueue(std::move(item));
        ++m_totalEnqueued;
        m_notEmpty.wakeOne();
        return true;
    }

    /**
     * @brief 入队操作（非阻塞版本）
     * 
     * 如果队列已满，立即返回false而不阻塞。
     * 
     * @param item 要入队的元素
     * @return true 成功入队，false 队列已满或已停止
     */
    bool tryEnqueue(const T& item)
    {
        QMutexLocker locker(&m_mutex);
        
        if (m_stopped || (m_maxSize > 0 && m_queue.size() >= m_maxSize)) {
            return false;
        }
        
        m_queue.enqueue(item);
        ++m_totalEnqueued;
        m_notEmpty.wakeOne();
        return true;
    }

    /**
     * @brief 入队操作（超时版本）
     * 
     * 在指定时间内等待队列有空间。
     * 
     * @param item 要入队的元素
     * @param timeoutMs 超时时间（毫秒）
     * @return true 成功入队，false 超时或队列已停止
     */
    bool enqueue(const T& item, int timeoutMs)
    {
        QMutexLocker locker(&m_mutex);
        
        if (!m_stopped && m_maxSize > 0 && m_queue.size() >= m_maxSize) {
            if (!m_notFull.wait(&m_mutex, timeoutMs)) {
                return false; // 超时
            }
        }
        
        if (m_stopped) {
            return false;
        }
        
        m_queue.enqueue(item);
        ++m_totalEnqueued;
        m_notEmpty.wakeOne();
        return true;
    }

    /**
     * @brief 出队操作（阻塞版本）
     * 
     * 如果队列为空，会阻塞等待直到有元素或队列被停止。
     * 
     * @param item 输出参数，存储出队的元素
     * @return true 成功出队，false 队列已停止且为空
     */
    bool dequeue(T& item)
    {
        QMutexLocker locker(&m_mutex);
        
        // 等待队列有元素或被停止
        while (!m_stopped && m_queue.isEmpty()) {
            m_notEmpty.wait(&m_mutex);
        }
        
        if (m_queue.isEmpty()) {
            return false; // 队列已停止且为空
        }
        
        item = m_queue.dequeue();
        ++m_totalDequeued;
        m_notFull.wakeOne();
        return true;
    }

    /**
     * @brief 出队操作（非阻塞版本）
     * 
     * 如果队列为空，立即返回false而不阻塞。
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
        m_notFull.wakeOne();
        return true;
    }

    /**
     * @brief 出队操作（超时版本）
     * 
     * 在指定时间内等待队列有元素。
     * 
     * @param item 输出参数，存储出队的元素
     * @param timeoutMs 超时时间（毫秒）
     * @return true 成功出队，false 超时或队列已停止且为空
     */
    bool dequeue(T& item, int timeoutMs)
    {
        QMutexLocker locker(&m_mutex);
        
        if (m_queue.isEmpty() && !m_stopped) {
            if (!m_notEmpty.wait(&m_mutex, timeoutMs)) {
                return false; // 超时
            }
        }
        
        if (m_queue.isEmpty()) {
            return false; // 队列已停止且为空
        }
        
        item = m_queue.dequeue();
        ++m_totalDequeued;
        m_notFull.wakeOne();
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
        m_notFull.wakeAll();
    }

    /**
     * @brief 停止队列操作
     * 
     * 停止后，所有阻塞的入队和出队操作都会返回false。
     * 已有的元素仍可以通过非阻塞方式出队。
     */
    void stop()
    {
        QMutexLocker locker(&m_mutex);
        m_stopped = true;
        m_notEmpty.wakeAll();
        m_notFull.wakeAll();
    }

    /**
     * @brief 重新启动队列
     */
    void restart()
    {
        QMutexLocker locker(&m_mutex);
        m_stopped = false;
    }

    /**
     * @brief 检查队列是否已停止
     * @return true 队列已停止，false 队列正常运行
     */
    bool isStopped() const
    {
        QMutexLocker locker(&m_mutex);
        return m_stopped;
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
        if (maxSize == 0 || m_queue.size() < maxSize) {
            m_notFull.wakeAll();
        }
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

private:
    mutable QMutex m_mutex;           ///< 互斥锁
    QWaitCondition m_notEmpty;        ///< 非空条件变量
    QWaitCondition m_notFull;         ///< 非满条件变量
    QQueue<T> m_queue;                ///< 底层队列
    int m_maxSize;                    ///< 最大容量
    bool m_stopped;                   ///< 停止标志
    quint64 m_totalEnqueued;          ///< 总入队数量
    quint64 m_totalDequeued;          ///< 总出队数量
};

#endif // THREADSAFEQUEUE_H