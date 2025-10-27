#pragma once

#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtGui/QImage>            // 修正：QImage 属于 QtGui 模块
#include <QtCore/QLoggingCategory>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QHash>            // 新增：使用QHash需包含此头
#include <memory>                  // 新增：std::unique_ptr

#include "../../common/data/DataRecord.h"

Q_DECLARE_LOGGING_CATEGORY(DataProcessingLog)

// 数据验证器：负责对接收到的捕获数据进行基本校验
// 责任边界：
// - 校验基本属性（非空、长度、类型）
// - 为图像类数据尝试解码并核验尺寸等信息
// - 产出 DataRecord，供后续清洗与格式化使用
class DataValidator final : public QObject {
    Q_OBJECT
public:
    explicit DataValidator(QObject* parent = nullptr);
    ~DataValidator() override;

    // 验证输入数据，并输出 DataRecord（若验证失败，返回 false）
    // 参数：raw        原始字节流（来自捕获模块，例如屏幕图像编码输出）
    //      mimeType   外部标注的MIME类型，例如 "image/png"、"application/octet-stream"
    //      recordOut  验证通过后输出的记录
    // 返回：验证是否成功
    bool validate(const QByteArray& raw, const QString& mimeType, DataRecord& recordOut);
};

// 数据清洗与格式化：
// - 清洗（过滤不可用数据、去除无效头部、统一编码等）
// - 格式化（根据业务约定统一输出结构，如PNG统一为RGBA）
class DataCleanerFormatter final : public QObject {
    Q_OBJECT
public:
    explicit DataCleanerFormatter(QObject* parent = nullptr);
    ~DataCleanerFormatter() override;

    // 对数据记录进行清洗与格式化；返回是否成功，失败时错误信息写入 errorOut
    bool cleanAndFormat(const DataRecord& in, DataRecord& out, QString& errorOut);
};

// 数据存储接口：抽象存储层，支持不同实现（内存/文件/数据库）
class IDataStore : public QObject {
    Q_OBJECT
public:
    explicit IDataStore(QObject* parent = nullptr) : QObject(parent) {}
    ~IDataStore() override = default;

    // 保存一条记录（若id冲突则覆盖或返回错误，具体由实现决定）
    virtual bool save(const DataRecord& record, QString& errorOut) = 0;
    // 根据ID检索记录
    virtual bool get(const QString& id, DataRecord& out, QString& errorOut) const = 0;
    // 删除记录
    virtual bool remove(const QString& id, QString& errorOut) = 0;
    // 统计当前存储数量
    virtual int count() const = 0;
};

// 内存存储实现：线程安全，基于QMutex保护
class InMemoryDataStore final : public IDataStore {
    Q_OBJECT
public:
    explicit InMemoryDataStore(QObject* parent = nullptr);
    ~InMemoryDataStore() override;

    bool save(const DataRecord& record, QString& errorOut) override;
    bool get(const QString& id, DataRecord& out, QString& errorOut) const override;
    bool remove(const QString& id, QString& errorOut) override;
    int count() const override;

private:
    mutable QMutex m_mutex;                 // 线程安全保护
    QHash<QString, DataRecord> m_storage;   // 内存哈希存储
};

// 高层数据处理器：将验证、清洗、存储串联，提供统一接口
class DataProcessor final : public QObject {
    Q_OBJECT
public:
    explicit DataProcessor(QObject* parent = nullptr);
    ~DataProcessor() override;

    // 接收原始数据并完成校验、清洗、格式化与存储
    // 返回：成功与否；错误写入 errorOut
    bool processAndStore(const QByteArray& raw, const QString& mimeType, QString& outId, QString& errorOut);

    // 根据ID检索处理后的记录
    bool retrieve(const QString& id, DataRecord& out, QString& errorOut) const;

    // 注入/替换存储实现（便于测试与扩展）
    void setStore(std::unique_ptr<IDataStore> store);

private:
    std::unique_ptr<IDataStore> m_store;            // 存储实现
    std::unique_ptr<DataValidator> m_validator;     // 验证器
    std::unique_ptr<DataCleanerFormatter> m_cleaner;// 清洗与格式化
};