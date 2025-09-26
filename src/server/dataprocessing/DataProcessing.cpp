#include "DataProcessing.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QUuid>
#include <QtCore/QBuffer>
#include <QtGui/QImageReader>   // 修正：QImageReader 属于 QtGui 模块
#include <QtCore/QCoreApplication>
#include <QtCore/QLoggingCategory>
#include <QtCore/QDateTime>
#include <cstring>

Q_LOGGING_CATEGORY(DataProcessingLog, "server.dataprocessing")

// ========== DataValidator 实现 ==========
DataValidator::DataValidator(QObject* parent) : QObject(parent) {}
DataValidator::~DataValidator() {}

bool DataValidator::validate(const QByteArray& raw, const QString& mimeType, DataRecord& recordOut) {
    // 基本非空校验
    if (raw.isEmpty()) {
        qCWarning(DataProcessingLog) << "验证失败：原始数据为空";
        return false;
    }
    if (mimeType.isEmpty()) {
        qCWarning(DataProcessingLog) << "验证失败：MIME类型为空";
        return false;
    }

    // 计算校验和（使用SHA-256截断为64位）
    const QByteArray hash = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
    quint64 checksum = 0;
    // 简单将前8字节拷贝为quint64（注意大小端，本处以小端序直接解释）
    if (hash.size() >= 8) {
        memcpy(&checksum, hash.constData(), 8);
    }

    recordOut.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    recordOut.timestamp = QDateTime::currentDateTimeUtc();
    recordOut.mimeType = mimeType;
    recordOut.payload = raw;
    recordOut.checksum = checksum;

    // 若为图像类型，尝试解码以确认基本尺寸
    if (mimeType.startsWith("image/")) {
        QBuffer buffer;
        buffer.setData(raw);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, mimeType.section('/', 1).toUtf8());
        const QImage img = reader.read();
        if (img.isNull()) {
            qCWarning(DataProcessingLog) << "验证失败：图像解码失败" << reader.errorString();
            return false;
        }
        recordOut.size = img.size();
    }

    return true;
}

// ========== DataCleanerFormatter 实现 ==========
DataCleanerFormatter::DataCleanerFormatter(QObject* parent) : QObject(parent) {}
DataCleanerFormatter::~DataCleanerFormatter() {}

bool DataCleanerFormatter::cleanAndFormat(const DataRecord& in, DataRecord& out, QString& errorOut) {
    // 基础复制，作为默认清洗策略：
    // - 去除两端可能的无效空字节（示例策略）
    // - 若为图像：统一为ARGB32格式以便后续处理（示例策略）
    out = in;

    // 去除首尾空字节（示例）：
    int start = 0;
    int end = out.payload.size();
    while (start < end && out.payload.at(start) == '\0') ++start;
    while (end > start && out.payload.at(end - 1) == '\0') --end;
    if (start > 0 || end < out.payload.size()) {
        out.payload = out.payload.mid(start, end - start);
    }

    if (out.mimeType.startsWith("image/")) {
        QBuffer buffer;
        buffer.setData(out.payload);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, out.mimeType.section('/', 1).toUtf8());
        QImage img = reader.read();
        if (img.isNull()) {
            errorOut = QStringLiteral("清洗失败：图像解码失败：%1").arg(reader.errorString());
            qCWarning(DataProcessingLog) << errorOut;
            return false;
        }
        // 统一为ARGB32格式
        QImage formatted = img.convertToFormat(QImage::Format_ARGB32);
        out.size = formatted.size();

        // 将格式化后的像素数据写回 payload（示例：原始像素字节）
        QByteArray pixels;
        pixels.resize(formatted.sizeInBytes());
        memcpy(pixels.data(), formatted.constBits(), formatted.sizeInBytes());
        out.payload = pixels;
        out.mimeType = QStringLiteral("application/x-raw-argb32");
    }

    return true;
}

// ========== InMemoryDataStore 实现 ==========
InMemoryDataStore::InMemoryDataStore(QObject* parent) : IDataStore(parent) {}
InMemoryDataStore::~InMemoryDataStore() {}

bool InMemoryDataStore::save(const DataRecord& record, QString& errorOut) {
    if (record.id.isEmpty()) {
        errorOut = QStringLiteral("存储失败：记录ID为空");
        qCWarning(DataProcessingLog) << errorOut;
        return false;
    }
    QMutexLocker locker(&m_mutex);
    m_storage.insert(record.id, record);
    qCDebug(DataProcessingLog) << "已保存记录" << record.id << "当前总数:" << m_storage.size();
    return true;
}

bool InMemoryDataStore::get(const QString& id, DataRecord& out, QString& errorOut) const {
    if (id.isEmpty()) {
        errorOut = QStringLiteral("检索失败：ID为空");
        qCWarning(DataProcessingLog) << errorOut;
        return false;
    }
    QMutexLocker locker(&m_mutex);
    auto it = m_storage.constFind(id);
    if (it == m_storage.constEnd()) {
        errorOut = QStringLiteral("检索失败：未找到ID=%1").arg(id);
        qCWarning(DataProcessingLog) << errorOut;
        return false;
    }
    out = it.value();
    return true;
}

bool InMemoryDataStore::remove(const QString& id, QString& errorOut) {
    if (id.isEmpty()) {
        errorOut = QStringLiteral("删除失败：ID为空");
        qCWarning(DataProcessingLog) << errorOut;
        return false;
    }
    QMutexLocker locker(&m_mutex);
    const int removed = m_storage.remove(id);
    if (removed == 0) {
        errorOut = QStringLiteral("删除失败：未找到ID=%1").arg(id);
        qCWarning(DataProcessingLog) << errorOut;
        return false;
    }
    qCDebug(DataProcessingLog) << "已删除记录" << id << "当前总数:" << m_storage.size();
    return true;
}

int InMemoryDataStore::count() const {
    QMutexLocker locker(&m_mutex);
    return m_storage.size();
}

// ========== DataProcessor 实现 ==========
DataProcessor::DataProcessor(QObject* parent)
    : QObject(parent)
    , m_store(std::make_unique<InMemoryDataStore>())
    , m_validator(std::make_unique<DataValidator>())
    , m_cleaner(std::make_unique<DataCleanerFormatter>()) {
}

DataProcessor::~DataProcessor() {}

void DataProcessor::setStore(std::unique_ptr<IDataStore> store) {
    if (store) {
        m_store = std::move(store);
    }
}

bool DataProcessor::processAndStore(const QByteArray& raw, const QString& mimeType, QString& outId, QString& errorOut) {
    DataRecord validated;
    if (!m_validator->validate(raw, mimeType, validated)) {
        errorOut = QStringLiteral("处理失败：数据验证不通过");
        return false;
    }

    DataRecord cleaned;
    if (!m_cleaner->cleanAndFormat(validated, cleaned, errorOut)) {
        // errorOut 由清洗组件填充
        return false;
    }

    if (!m_store->save(cleaned, errorOut)) {
        // errorOut 由存储层填充
        return false;
    }

    outId = cleaned.id;
    return true;
}

bool DataProcessor::retrieve(const QString& id, DataRecord& out, QString& errorOut) const {
    return m_store->get(id, out, errorOut);
}