#ifndef ZEROCOPYDATA_H
#define ZEROCOPYDATA_H

#include <QtCore/QSharedPointer>
#include <QtCore/QWeakPointer>
#include <QtCore/QAtomicInt>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtGui/QImage>
#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>

/**
 * @brief 零拷贝数据基类
 * 
 * 提供引用计数和生命周期管理，支持在多线程间安全传递数据而无需拷贝。
 */
class ZeroCopyDataBase
{
public:
    /**
     * @brief 构造函数
     */
    ZeroCopyDataBase()
        : m_timestamp(QDateTime::currentMSecsSinceEpoch())
        , m_refCount(1)
    {
    }

    /**
     * @brief 虚析构函数
     */
    virtual ~ZeroCopyDataBase() = default;

    /**
     * @brief 获取时间戳
     * @return 创建时间戳（毫秒）
     */
    qint64 timestamp() const { return m_timestamp; }

    /**
     * @brief 获取引用计数
     * @return 当前引用计数
     */
    int refCount() const { return m_refCount.loadAcquire(); }

    /**
     * @brief 增加引用计数
     */
    void addRef() { m_refCount.ref(); }

    /**
     * @brief 减少引用计数
     * @return 如果引用计数为0返回true
     */
    bool release() { return !m_refCount.deref(); }

    /**
     * @brief 获取数据大小（字节）
     * @return 数据大小
     */
    virtual qint64 dataSize() const = 0;

    /**
     * @brief 获取数据类型名称
     * @return 类型名称
     */
    virtual QString typeName() const = 0;

private:
    qint64 m_timestamp;         ///< 创建时间戳
    QAtomicInt m_refCount;      ///< 引用计数
};

/**
 * @brief 零拷贝图像数据
 * 
 * 封装QImage数据，支持零拷贝传递和共享。
 */
class ZeroCopyImageData : public ZeroCopyDataBase
{
public:
    /**
     * @brief 构造函数
     * @param image 图像数据
     */
    explicit ZeroCopyImageData(const QImage& image)
        : ZeroCopyDataBase()
        , m_image(image)
        , m_compressed(false)
    {
    }

    /**
     * @brief 构造函数（移动语义）
     * @param image 图像数据
     */
    explicit ZeroCopyImageData(QImage&& image)
        : ZeroCopyDataBase()
        , m_image(std::move(image))
        , m_compressed(false)
    {
    }

    /**
     * @brief 获取图像数据
     * @return 图像引用
     */
    const QImage& image() const { return m_image; }

    /**
     * @brief 获取可修改的图像数据
     * @return 图像引用
     */
    QImage& image() { return m_image; }

    /**
     * @brief 设置压缩数据
     * @param data 压缩后的数据
     * @param format 压缩格式
     */
    void setCompressedData(const QByteArray& data, const QString& format)
    {
        QMutexLocker locker(&m_mutex);
        m_compressedData = data;
        m_compressionFormat = format;
        m_compressed = true;
    }

    /**
     * @brief 获取压缩数据
     * @return 压缩数据
     */
    QByteArray compressedData() const
    {
        QMutexLocker locker(&m_mutex);
        return m_compressedData;
    }

    /**
     * @brief 获取压缩格式
     * @return 压缩格式
     */
    QString compressionFormat() const
    {
        QMutexLocker locker(&m_mutex);
        return m_compressionFormat;
    }

    /**
     * @brief 是否已压缩
     * @return 压缩状态
     */
    bool isCompressed() const
    {
        QMutexLocker locker(&m_mutex);
        return m_compressed;
    }

    /**
     * @brief 获取数据大小
     * @return 数据大小（字节）
     */
    qint64 dataSize() const override
    {
        qint64 imageSize = m_image.sizeInBytes();
        QMutexLocker locker(&m_mutex);
        qint64 compressedSize = m_compressed ? m_compressedData.size() : 0;
        return imageSize + compressedSize;
    }

    /**
     * @brief 获取类型名称
     * @return 类型名称
     */
    QString typeName() const override
    {
        return QStringLiteral("ZeroCopyImageData");
    }

    /**
     * @brief 获取图像信息
     * @return 图像信息字符串
     */
    QString imageInfo() const
    {
        return QString("Size: %1x%2, Format: %3, Depth: %4")
            .arg(m_image.width())
            .arg(m_image.height())
            .arg(static_cast<int>(m_image.format()))
            .arg(m_image.depth());
    }

private:
    QImage m_image;                     ///< 图像数据
    mutable QMutex m_mutex;             ///< 互斥锁
    QByteArray m_compressedData;        ///< 压缩数据
    QString m_compressionFormat;        ///< 压缩格式
    bool m_compressed;                  ///< 是否已压缩
};

/**
 * @brief 零拷贝字节数组数据
 * 
 * 封装QByteArray数据，支持零拷贝传递和共享。
 */
class ZeroCopyByteArrayData : public ZeroCopyDataBase
{
public:
    /**
     * @brief 构造函数
     * @param data 字节数组数据
     */
    explicit ZeroCopyByteArrayData(const QByteArray& data)
        : ZeroCopyDataBase()
        , m_data(data)
    {
    }

    /**
     * @brief 构造函数（移动语义）
     * @param data 字节数组数据
     */
    explicit ZeroCopyByteArrayData(QByteArray&& data)
        : ZeroCopyDataBase()
        , m_data(std::move(data))
    {
    }

    /**
     * @brief 获取字节数组数据
     * @return 数据引用
     */
    const QByteArray& data() const { return m_data; }

    /**
     * @brief 获取可修改的字节数组数据
     * @return 数据引用
     */
    QByteArray& data() { return m_data; }

    /**
     * @brief 获取数据大小
     * @return 数据大小（字节）
     */
    qint64 dataSize() const override
    {
        return m_data.size();
    }

    /**
     * @brief 获取类型名称
     * @return 类型名称
     */
    QString typeName() const override
    {
        return QStringLiteral("ZeroCopyByteArrayData");
    }

private:
    QByteArray m_data;  ///< 字节数组数据
};

/**
 * @brief 零拷贝数据智能指针
 * 
 * 提供自动内存管理和线程安全的数据共享。
 */
template<typename T>
class ZeroCopyPtr
{
public:
    /**
     * @brief 默认构造函数
     */
    ZeroCopyPtr() : m_data(nullptr) {}

    /**
     * @brief 构造函数
     * @param data 数据指针
     */
    explicit ZeroCopyPtr(T* data) : m_data(data) {}

    /**
     * @brief 拷贝构造函数
     * @param other 其他智能指针
     */
    ZeroCopyPtr(const ZeroCopyPtr& other) : m_data(other.m_data)
    {
        if (m_data) {
            m_data->addRef();
        }
    }

    /**
     * @brief 移动构造函数
     * @param other 其他智能指针
     */
    ZeroCopyPtr(ZeroCopyPtr&& other) noexcept : m_data(other.m_data)
    {
        other.m_data = nullptr;
    }

    /**
     * @brief 析构函数
     */
    ~ZeroCopyPtr()
    {
        reset();
    }

    /**
     * @brief 拷贝赋值操作符
     * @param other 其他智能指针
     * @return 自身引用
     */
    ZeroCopyPtr& operator=(const ZeroCopyPtr& other)
    {
        if (this != &other) {
            reset();
            m_data = other.m_data;
            if (m_data) {
                m_data->addRef();
            }
        }
        return *this;
    }

    /**
     * @brief 移动赋值操作符
     * @param other 其他智能指针
     * @return 自身引用
     */
    ZeroCopyPtr& operator=(ZeroCopyPtr&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_data = other.m_data;
            other.m_data = nullptr;
        }
        return *this;
    }

    /**
     * @brief 解引用操作符
     * @return 数据引用
     */
    T& operator*() const
    {
        Q_ASSERT(m_data);
        return *m_data;
    }

    /**
     * @brief 成员访问操作符
     * @return 数据指针
     */
    T* operator->() const
    {
        return m_data;
    }

    /**
     * @brief 获取原始指针
     * @return 数据指针
     */
    T* get() const
    {
        return m_data;
    }

    /**
     * @brief 检查是否为空
     * @return 是否为空
     */
    bool isNull() const
    {
        return m_data == nullptr;
    }

    /**
     * @brief 布尔转换操作符
     * @return 是否非空
     */
    explicit operator bool() const
    {
        return m_data != nullptr;
    }

    /**
     * @brief 重置指针
     */
    void reset()
    {
        if (m_data && m_data->release()) {
            delete m_data;
        }
        m_data = nullptr;
    }

    /**
     * @brief 重置指针并设置新数据
     * @param data 新数据指针
     */
    void reset(T* data)
    {
        reset();
        m_data = data;
    }

    /**
     * @brief 获取引用计数
     * @return 引用计数
     */
    int refCount() const
    {
        return m_data ? m_data->refCount() : 0;
    }

private:
    T* m_data;  ///< 数据指针
};

// 类型别名
using ZeroCopyImagePtr = ZeroCopyPtr<ZeroCopyImageData>;
using ZeroCopyByteArrayPtr = ZeroCopyPtr<ZeroCopyByteArrayData>;

/**
 * @brief 创建零拷贝图像数据
 * @param image 图像数据
 * @return 零拷贝图像指针
 */
inline ZeroCopyImagePtr makeZeroCopyImage(const QImage& image)
{
    return ZeroCopyImagePtr(new ZeroCopyImageData(image));
}

/**
 * @brief 创建零拷贝图像数据（移动语义）
 * @param image 图像数据
 * @return 零拷贝图像指针
 */
inline ZeroCopyImagePtr makeZeroCopyImage(QImage&& image)
{
    return ZeroCopyImagePtr(new ZeroCopyImageData(std::move(image)));
}

/**
 * @brief 创建零拷贝字节数组数据
 * @param data 字节数组数据
 * @return 零拷贝字节数组指针
 */
inline ZeroCopyByteArrayPtr makeZeroCopyByteArray(const QByteArray& data)
{
    return ZeroCopyByteArrayPtr(new ZeroCopyByteArrayData(data));
}

/**
 * @brief 创建零拷贝字节数组数据（移动语义）
 * @param data 字节数组数据
 * @return 零拷贝字节数组指针
 */
inline ZeroCopyByteArrayPtr makeZeroCopyByteArray(QByteArray&& data)
{
    return ZeroCopyByteArrayPtr(new ZeroCopyByteArrayData(std::move(data)));
}

#endif // ZEROCOPYDATA_H